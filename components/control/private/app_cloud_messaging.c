#include "app_cloud_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_cloud_cmd.h"

// MQTT 消息层：负责云端命令解析、ACK 回复和任务状态 JSON 上报。

static const char *TAG = "app_cloud";
// cJSON 添加字段的轻量包装，让上报构造可以串联判断内存失败。
static bool app_cloud_json_add_string(cJSON *root, const char *key, const char *value)
{
    return cJSON_AddStringToObject(root, key, (value != NULL) ? value : "") != NULL;
}
static bool app_cloud_json_add_number(cJSON *root, const char *key, double value)
{
    return cJSON_AddNumberToObject(root, key, value) != NULL;
}
// 订单名未下发时，用订单号后 6 位生成一个现场可读名称。
static void app_cloud_make_order_name(const char *order_id, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U)
    {
        return;
    }
    out[0] = '\0';
    if (order_id == NULL || order_id[0] == '\0')
    {
        strlcpy(out, "SKY-ORDER", out_size);
        return;
    }
    const char *suffix = strrchr(order_id, '-');
    suffix = (suffix != NULL && suffix[1] != '\0') ? (suffix + 1) : order_id;
    size_t suffix_len = strlen(suffix);
    if (suffix_len > 6U)
    {
        suffix += suffix_len - 6U;
    }
    snprintf(out, out_size, "SKY-%s", suffix);
}
static const char *app_cloud_order_id_from_cmd(const app_cloud_cmd_t *cmd)
{
    if (cmd == NULL)
    {
        return "";
    }
    if (cmd->order_id[0] != '\0')
    {
        return cmd->order_id;
    }
    return cmd->request_id;
}
static void app_cloud_order_name_from_cmd(const app_cloud_cmd_t *cmd,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0U)
    {
        return;
    }
    if (cmd != NULL && cmd->order_name[0] != '\0')
    {
        strlcpy(out, cmd->order_name, out_size);
        return;
    }
    app_cloud_make_order_name(app_cloud_order_id_from_cmd(cmd), out, out_size);
}
static const char *app_cloud_weather_mode_text(app_cloud_weather_mode_t mode)
{
    switch (mode) {
    case APP_CLOUD_WEATHER_MODE_CLOUD_GUARD:
        return "cloud_guard";
    case APP_CLOUD_WEATHER_MODE_EMERGENCY:
        return "emergency";
    case APP_CLOUD_WEATHER_MODE_NORMAL:
    default:
        return "normal";
    }
}
static esp_err_t app_cloud_publish_raw(const char *topic,
    const char *payload,
    int qos,
    int retain);
// JSON 对象发布后由调用者继续负责释放 root。
static esp_err_t app_cloud_publish_json(const char *topic, cJSON *root, int retain)
{
    if (topic == NULL || root == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    char *json = cJSON_PrintUnformatted(root);
    if (json == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = app_cloud_publish_raw(topic, json, CONFIG_SKY_MQTT_QOS, retain);
    cJSON_free(json);
    return ret;
}
// MQTT 原始发布，连接不可用时返回状态错误供上层稍后重发。
static esp_err_t app_cloud_publish_raw(const char *topic,
    const char *payload,
    int qos,
    int retain)
{
    if (topic == NULL || payload == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_cloud.mqtt_client == NULL || !s_cloud.mqtt_connected)
    {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_cloud.mqtt_client, topic, payload, 0, qos, retain);
    if (msg_id < 0)
    {
        ESP_LOGW(TAG, "mqtt publish failed, topic=%s", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}
// 给云端命令回复 ACK，包含请求/订单信息和执行结果码。
static esp_err_t app_cloud_publish_ack(const app_cloud_cmd_t *cmd,
    int code,
    const char *msg,
    uint16_t target_id)
{
    if (cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    const char *order_id = (cmd->order_id[0] != '\0') ? cmd->order_id : s_cloud.current_order_id;
    char order_name[sizeof(s_cloud.current_order_name)] = {0};
    if (cmd->order_name[0] != '\0')
    {
        strlcpy(order_name, cmd->order_name, sizeof(order_name));
    }
    else if (s_cloud.current_order_name[0] != '\0')
    {
        strlcpy(order_name, s_cloud.current_order_name, sizeof(order_name));
    }
    else
    {
        app_cloud_make_order_name(order_id, order_name, sizeof(order_name));
    }
    bool ok = app_cloud_json_add_string(root, "request_id", cmd->request_id) &&
        app_cloud_json_add_string(root, "order_id", order_id) &&
        app_cloud_json_add_string(root, "order_name", order_name) &&
        app_cloud_json_add_string(root, "cmd", cmd->cmd) &&
        app_cloud_json_add_number(root, "code", code) &&
        app_cloud_json_add_string(root, "msg", (msg != NULL) ? msg : "-") &&
        app_cloud_json_add_number(root, "target_id", target_id);
    esp_err_t ret = ok ? app_cloud_publish_json(s_cloud.topic_ack, root, 0) : ESP_ERR_NO_MEM;
    cJSON_Delete(root);
    return ret;
}
// 发布任务状态快照；retain=1 让云端/调试端重连后能看到最近状态。
void app_cloud_publish_task_snapshot_internal(const app_task_snapshot_t *snap)
{
    if (snap == NULL)
    {
        return;
    }
    uint32_t seq = ++s_cloud.msg_seq;
    time_t now = 0;
    time(&now);
    int cargo_received = (snap->state == APP_TASK_STATE_COMPLETED) ? 1 : 0;
    int fault = (snap->state == APP_TASK_STATE_FAULT) ? 1 : 0;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGW(TAG, "task snapshot not sent yet: no memory");
        return;
    }
    bool ok = app_cloud_json_add_number(root, "msg_id", seq) &&
        app_cloud_json_add_number(root, "ts", (double)now) &&
        app_cloud_json_add_string(root, "request_id", s_cloud.current_request_id) &&
        app_cloud_json_add_string(root, "order_id", s_cloud.current_order_id) &&
        app_cloud_json_add_string(root, "order_name", s_cloud.current_order_name) &&
        app_cloud_json_add_string(root, "device", CONFIG_SKY_MQTT_DEVICE_NAME) &&
        app_cloud_json_add_string(root, "state", app_task_state_to_text(snap->state)) &&
        app_cloud_json_add_number(root, "active", snap->active ? 1U : 0U) &&
        app_cloud_json_add_number(root, "target_id", snap->target_id) &&
        app_cloud_json_add_number(root, "matched_tag_id", snap->matched_tag_id) &&
        app_cloud_json_add_number(root, "cargo_received", cargo_received) &&
        app_cloud_json_add_number(root, "fault", fault) &&
        app_cloud_json_add_number(root, "weather_blocked", s_cloud.weather_docking_blocked ? 1U : 0U) &&
        app_cloud_json_add_number(root, "weather_simulated", s_cloud.weather_simulated ? 1U : 0U) &&
        app_cloud_json_add_number(root, "accept_orders", s_cloud.weather_docking_blocked ? 0U : 1U) &&
        app_cloud_json_add_string(root, "weather_mode", app_cloud_weather_mode_text(s_cloud.weather_mode)) &&
        app_cloud_json_add_string(root, "source", snap->source) &&
        app_cloud_json_add_string(root, "note", snap->note);
    esp_err_t ret = ok ? app_cloud_publish_json(s_cloud.topic_state, root, 1) : ESP_ERR_NO_MEM;
    cJSON_Delete(root);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "task snapshot not sent yet: %s", esp_err_to_name(ret));
    }
}
// 主动发布当前任务状态，天气策略变化或命令拒绝时调用。
void app_cloud_publish_current_state(void)
{
    app_task_snapshot_t snap = {0};
    if (app_task_peek_snapshot(&snap))
    {
        s_cloud.last_snapshot = snap;
        s_cloud.have_last_snapshot = true;
        app_cloud_publish_task_snapshot_internal(&snap);
        return;
    }
    if (s_cloud.have_last_snapshot)
    {
        app_cloud_publish_task_snapshot_internal(&s_cloud.last_snapshot);
    }
}
// 任务模块事件回调：缓存最新快照并推送到 MQTT。
void app_cloud_on_task_event(app_task_event_t event,
    const app_task_snapshot_t *snap,
    void *user_ctx)
{
    (void)event;
    (void)user_ctx;
    if (snap == NULL)
    {
        return;
    }
    s_cloud.last_snapshot = *snap;
    s_cloud.have_last_snapshot = true;
    app_cloud_publish_task_snapshot_internal(snap);
}
static esp_err_t app_cloud_receive_set_target(uint16_t target_id)
{
    return app_task_set_target_id(target_id, true);
}
// 处理云端 start_task；天气保护期间拒绝接单并保持原订单上下文。
static esp_err_t app_cloud_receive_start_task(const app_cloud_cmd_t *cmd)
{
    if (cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_cloud.weather_docking_blocked)
    {
        ESP_LOGW(TAG,
            "cloud rx: reject start_task for weather target=%u request_id=%s",
            (unsigned)cmd->target_id,
            (cmd->request_id[0] != '\0') ? cmd->request_id : "-");
        app_cloud_publish_current_state();
        return ESP_ERR_INVALID_STATE;
    }
    char previous_request_id[sizeof(s_cloud.current_request_id)];
    char previous_order_id[sizeof(s_cloud.current_order_id)];
    char previous_order_name[sizeof(s_cloud.current_order_name)];
    strlcpy(previous_request_id, s_cloud.current_request_id, sizeof(previous_request_id));
    strlcpy(previous_order_id, s_cloud.current_order_id, sizeof(previous_order_id));
    strlcpy(previous_order_name, s_cloud.current_order_name, sizeof(previous_order_name));
    strlcpy(s_cloud.current_request_id, cmd->request_id, sizeof(s_cloud.current_request_id));
    strlcpy(s_cloud.current_order_id,
        app_cloud_order_id_from_cmd(cmd),
        sizeof(s_cloud.current_order_id));
    app_cloud_order_name_from_cmd(cmd,
        s_cloud.current_order_name,
        sizeof(s_cloud.current_order_name));
    esp_err_t ret = app_task_submit_remote_request(cmd->target_id, "emqx");
    if (ret != ESP_OK)
    {
        strlcpy(s_cloud.current_request_id, previous_request_id, sizeof(s_cloud.current_request_id));
        strlcpy(s_cloud.current_order_id, previous_order_id, sizeof(s_cloud.current_order_id));
        strlcpy(s_cloud.current_order_name, previous_order_name, sizeof(s_cloud.current_order_name));
    }
    return ret;
}
static esp_err_t app_cloud_receive_cancel(void)
{
    app_task_cancel("cancelled by cloud");
    return ESP_OK;
}
// MQTT cmd 负载入口，按 cmd 字段分发到对应业务处理。
static esp_err_t app_cloud_handle_command(const char *payload, size_t payload_len)
{
    char json[APP_CLOUD_CMD_PAYLOAD_LEN];
    if (payload == NULL || payload_len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    size_t copy_len = payload_len;
    if (copy_len >= sizeof(json))
    {
        copy_len = sizeof(json) - 1U;
    }
    memcpy(json, payload, copy_len);
    json[copy_len] = '\0';
    app_cloud_cmd_t cmd = {0};
    esp_err_t ret = app_cloud_cmd_parse_json(json, &cmd);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "bad EMQX cmd payload, len=%u", (unsigned)copy_len);
        return ret;
    }
    if (strcmp(cmd.cmd, "start_task") == 0)
    {
        ret = app_cloud_receive_start_task(&cmd);
        (void)app_cloud_publish_ack(&cmd,
            (ret == ESP_OK) ? 0 : ((ret == ESP_ERR_INVALID_STATE && s_cloud.weather_docking_blocked) ? -3 : -1),
            (ret == ESP_OK) ? "accepted" :
                ((ret == ESP_ERR_INVALID_STATE && s_cloud.weather_docking_blocked) ? "weather_blocked" : "start_failed"),
            cmd.target_id);
        return ret;
    }
    if (strcmp(cmd.cmd, "set_target") == 0)
    {
        ret = app_cloud_receive_set_target(cmd.target_id);
        (void)app_cloud_publish_ack(&cmd,
            (ret == ESP_OK) ? 0 : -1,
            (ret == ESP_OK) ? "accepted" : "set_failed",
            cmd.target_id);
        return ret;
    }
    if (strcmp(cmd.cmd, "cancel") == 0)
    {
        ret = app_cloud_receive_cancel();
        (void)app_cloud_publish_ack(&cmd,
            (ret == ESP_OK) ? 0 : -1,
            (ret == ESP_OK) ? "accepted" : "cancel_failed",
            0U);
        return ret;
    }
    ESP_LOGW(TAG, "unknown cmd=%s", cmd.cmd);
    (void)app_cloud_publish_ack(&cmd, -2, "unknown_cmd", cmd.target_id);
    return ESP_ERR_NOT_SUPPORTED;
}
// ESP MQTT 事件里的 topic/data 不是 NUL 结尾，先复制成普通字符串。
static int app_cloud_copy_event_slice(char *dst, size_t dst_size, const char *src, int src_len)
{
    if ((dst == NULL) || (dst_size == 0U))
    {
        return 0;
    }
    int copy_len = src_len;
    if ((src == NULL) || (copy_len < 0))
    {
        copy_len = 0;
    }
    if ((size_t)copy_len >= dst_size)
    {
        copy_len = (int)dst_size - 1;
    }
    if (copy_len > 0)
    {
        memcpy(dst, src, (size_t)copy_len);
    }
    dst[copy_len] = '\0';
    return copy_len;
}
// MQTT DATA 事件入口，只处理本设备的 cmd 主题。
void app_cloud_handle_mqtt_data_event(esp_mqtt_event_handle_t event)
{
    if (event == NULL || event->topic == NULL || event->data == NULL)
    {
        return;
    }
    char topic[APP_CLOUD_TOPIC_BUF_LEN];
    char payload[APP_CLOUD_JSON_BUF_LEN];
    if (event->topic_len <= 0)
    {
        return;
    }
    (void)app_cloud_copy_event_slice(topic, sizeof(topic), event->topic, event->topic_len);
    const int data_len = app_cloud_copy_event_slice(payload, sizeof(payload), event->data, event->data_len);
    if (strcmp(topic, s_cloud.topic_cmd) == 0)
    {
        (void)app_cloud_handle_command(payload, (size_t)data_len);
    }
}
