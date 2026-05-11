/* 实现说明：云端启动失败时允许本地视觉和控制继续运行。 */
/*
 * app_cloud.c - Wi-Fi / MQTT 云端通信模块
 *
 * 这个文件负责把设备接入云端：
 * - 初始化 esp_netif、事件循环和 Wi-Fi STA；
 * - 建立 MQTT 客户端连接；
 * - 根据设备 ID 生成命令、应答、状态等 topic；
 * - 解析云端/小程序下发的 JSON 命令；
 * - 将 app_task.c 的任务状态快照发布到 MQTT。
 *
 * 在你的项目中，小程序或云端并不是直接控制电机，而是通过 MQTT 发任务命令给 ESP32-P4，
 * 再由 app_task/app_ctrl/app_ch32_link 分层处理，这样能保证“联网控制”和“本地安全状态机”分开。
 */

#include "app_cloud.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "app_task.h"

static const char *TAG = "app_cloud";

/* -------------------------------------------------------------------------- */
/* 网络、MQTT 和载荷尺寸                                           */
/* -------------------------------------------------------------------------- */

#define APP_CLOUD_WIFI_CONNECTED_BIT   BIT0
#define APP_CLOUD_MQTT_CONNECTED_BIT   BIT1
#define APP_CLOUD_START_MQTT_BIT       BIT2
#define APP_CLOUD_TASK_STACK_SIZE      (8 * 1024)
#define APP_CLOUD_TASK_PRIORITY        5
#define APP_CLOUD_TOPIC_BUF_LEN        192
#define APP_CLOUD_JSON_BUF_LEN         512
#define APP_CLOUD_CMD_PAYLOAD_LEN      256

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    char cmd[24];           /* 云端下发的命令名称。 */
    uint16_t target_id;     /* 命令携带的目标 tag ID。 */
    char request_id[32];    /* 云端请求 ID，用于应答消息关联。 */
} app_cloud_cmd_t;
typedef struct {
    bool inited;                                      /* 云端模块是否完成初始化。 */
    bool mqtt_started;                                /* MQTT 客户端是否已经启动。 */
    bool mqtt_connected;                              /* MQTT 当前是否已连接。 */
    bool have_last_snapshot;                          /* 是否缓存了最近一次任务快照。 */
    uint32_t msg_seq;                                 /* 发布状态消息时递增的本地序号。 */
    EventGroupHandle_t event_group;                   /* Wi-Fi/MQTT 状态同步事件组。 */
    esp_netif_t *sta_netif;                           /* Wi-Fi STA 网络接口句柄。 */
    esp_mqtt_client_handle_t mqtt_client;             /* MQTT 客户端句柄。 */
    app_task_snapshot_t last_snapshot;                /* 最近一次任务状态快照。 */
    char current_request_id[32];                      /* 当前正在处理的云端请求 ID。 */
    char topic_cmd[APP_CLOUD_TOPIC_BUF_LEN];          /* 订阅云端命令的 topic。 */
    char topic_ack[APP_CLOUD_TOPIC_BUF_LEN];          /* 发布命令应答的 topic。 */
    char topic_state[APP_CLOUD_TOPIC_BUF_LEN];        /* 发布任务状态的 topic。 */
} app_cloud_runtime_t;
static app_cloud_runtime_t s_cloud = {0};
static TaskHandle_t s_cloud_task = NULL;

/* MQTT 事件处理函数需要先声明，创建客户端时会注册它。 */
static void app_cloud_mqtt_event_handler(void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data);

static void app_cloud_log_heap(const char *stage)
{
    ESP_LOGI(TAG,
        "%s heap: int_free=%lu int_largest=%lu psram_free=%lu psram_largest=%lu",
        stage,
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

/* -------------------------------------------------------------------------- */
/* JSON 小工具                                                          */
/* -------------------------------------------------------------------------- */

/* 从简单 JSON 文本中提取字符串字段。 */
static bool app_cloud_json_get_string(const char *json,
    const char *key,
    char *out,
    size_t out_size)
{
    if (json == NULL || key == NULL || out == NULL || out_size == 0U)
    {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL)
    {
        return false;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':')
    {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"')
    {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1U < out_size) {
        if (*p == '\\' && p[1] != '\0')
        {
            p++;
        }
        out[i++] = *p++;
    }
    if (*p != '"')
    {
        return false;
    }
    out[i] = '\0';
    return true;
}
/* 从简单 JSON 文本中提取 uint16 数字字段。 */
static bool app_cloud_json_get_u16(const char *json,
    const char *key,
    uint16_t *out)
{
    if (json == NULL || key == NULL || out == NULL)
    {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL)
    {
        return false;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':')
    {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (!isdigit((unsigned char)*p))
    {
        return false;
    }
    unsigned value = 0U;
    while (isdigit((unsigned char)*p)) {
        value = value * 10U + (unsigned)(*p - '0');
        if (value > UINT16_MAX)
        {
            return false;
        }
        p++;
    }
    *out = (uint16_t)value;
    return true;
}
/* 解析云端命令 payload，得到命令名、目标 ID 和 request_id。 */
static esp_err_t app_cloud_parse_command_json(const char *payload, app_cloud_cmd_t *out)
{
    if (payload == NULL || out == NULL)
    {

        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!app_cloud_json_get_string(payload, "cmd", out->cmd, sizeof(out->cmd)))
    {

        return ESP_ERR_INVALID_ARG;
    }
    (void)app_cloud_json_get_u16(payload, "target_id", &out->target_id);
    (void)app_cloud_json_get_string(payload, "request_id", out->request_id, sizeof(out->request_id));
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Wi-Fi / MQTT 初始化辅助函数                                                  */
/* -------------------------------------------------------------------------- */

/* 初始化 esp_netif，已初始化时视为成功。 */
static esp_err_t app_cloud_init_netif_once(void)
{
    esp_err_t ret = esp_netif_init();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        return ESP_OK;
    }
    return ret;
}
/* 创建默认事件循环，已创建时视为成功。 */
static esp_err_t app_cloud_init_event_loop_once(void)
{
    esp_err_t ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        return ESP_OK;
    }
    return ret;
}
/* 根据 topic 前缀和设备名生成命令、应答和状态 topic。 */
static void app_cloud_build_topics(void)
{
    snprintf(s_cloud.topic_cmd,
        sizeof(s_cloud.topic_cmd),
        "%s/%s/cmd",
        CONFIG_SKY_MQTT_TOPIC_PREFIX,
        CONFIG_SKY_MQTT_DEVICE_NAME);
    snprintf(s_cloud.topic_ack,
        sizeof(s_cloud.topic_ack),
        "%s/%s/ack",
        CONFIG_SKY_MQTT_TOPIC_PREFIX,
        CONFIG_SKY_MQTT_DEVICE_NAME);
    snprintf(s_cloud.topic_state,
        sizeof(s_cloud.topic_state),
        "%s/%s/state",
        CONFIG_SKY_MQTT_TOPIC_PREFIX,
        CONFIG_SKY_MQTT_DEVICE_NAME);
}
/* 打印当前 MQTT topic，方便联调订阅路径。 */
static void app_cloud_log_topics_once(void)
{
    ESP_LOGD(TAG,
        "mqtt topics ready: cmd=%s ack=%s state=%s",
        s_cloud.topic_cmd,
        s_cloud.topic_ack,
        s_cloud.topic_state);
}
/* 检查 Wi-Fi 和 MQTT menuconfig 配置是否完整有效。 */
static esp_err_t app_cloud_validate_config(void)
{
    if (CONFIG_SKY_WIFI_SSID[0] == '\0')
    {
        ESP_LOGE(TAG, "CONFIG_SKY_WIFI_SSID is empty, please set it in menuconfig");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_BROKER_URI[0] == '\0')
    {
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_BROKER_URI is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_CLIENT_ID[0] == '\0')
    {
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_CLIENT_ID is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_DEVICE_NAME[0] == '\0')
    {
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_DEVICE_NAME is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_TOPIC_PREFIX[0] == '\0')
    {
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_TOPIC_PREFIX is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(CONFIG_SKY_WIFI_SSID) >= sizeof(((wifi_config_t *)0)->sta.ssid))
    {
        ESP_LOGE(TAG, "Wi-Fi SSID too long");
        return ESP_ERR_INVALID_SIZE;
    }
    if (strlen(CONFIG_SKY_WIFI_PASSWORD) >= sizeof(((wifi_config_t *)0)->sta.password))
    {
        ESP_LOGE(TAG, "Wi-Fi password too long");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* 发布辅助函数                                                             */
/* -------------------------------------------------------------------------- */

/* 向指定 MQTT topic 发布原始 payload。 */
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
    ESP_LOGD(TAG, "mqtt tx topic=%s msg_id=%d", topic, msg_id);
    return ESP_OK;
}
/* 发布云端命令处理结果 ACK。 */
static esp_err_t app_cloud_publish_ack(const app_cloud_cmd_t *cmd,
    int code,
    const char *msg,
    uint16_t target_id)
{
    if (cmd == NULL)
    {

        return ESP_ERR_INVALID_ARG;
    }
    char json[APP_CLOUD_JSON_BUF_LEN];
    snprintf(json,
        sizeof(json),
        "{"
        "\"request_id\":\"%s\","
        "\"cmd\":\"%s\","
        "\"code\":%d,"
        "\"msg\":\"%s\","
        "\"target_id\":%u"
        "}",
        cmd->request_id,
        cmd->cmd,
        code,
        (msg != NULL) ? msg : "-",
        (unsigned)target_id);
    return app_cloud_publish_raw(s_cloud.topic_ack, json, CONFIG_SKY_MQTT_QOS, 0);
}
/* 将任务快照组装成 JSON 并发布到状态 topic。 */
static void app_cloud_publish_task_snapshot_internal(const app_task_snapshot_t *snap)
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
    char json[APP_CLOUD_JSON_BUF_LEN];
    snprintf(json,
        sizeof(json),
        "{"
        "\"msg_id\":%u,"
        "\"ts\":%lld,"
        "\"request_id\":\"%s\","
        "\"device\":\"%s\","
        "\"state\":\"%s\","
        "\"active\":%u,"
        "\"target_id\":%u,"
        "\"matched_tag_id\":%u,"
        "\"cargo_received\":%d,"
        "\"fault\":%d,"
        "\"source\":\"%s\","
        "\"note\":\"%s\""
        "}",
        (unsigned)seq,
        (long long)now,
        s_cloud.current_request_id,
        CONFIG_SKY_MQTT_DEVICE_NAME,
        app_task_state_to_text(snap->state),
        snap->active ? 1U : 0U,
        (unsigned)snap->target_id,
        (unsigned)snap->matched_tag_id,
        cargo_received,
        fault,
        snap->source,
        snap->note);

    esp_err_t ret = app_cloud_publish_raw(s_cloud.topic_state, json, CONFIG_SKY_MQTT_QOS, 1);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "task snapshot not sent yet: %s", esp_err_to_name(ret));
    }
}

/* -------------------------------------------------------------------------- */
/* 任务和云端命令处理                                             */
/* -------------------------------------------------------------------------- */

/* app_task 状态变化回调，用于缓存并发布最新任务快照。 */
static void app_cloud_on_task_event(app_task_event_t event,
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
/* 创建 MQTT 客户端实例并填入连接、认证和证书配置。 */
static esp_err_t app_cloud_create_mqtt_client(void)
{
    if (s_cloud.mqtt_client != NULL)
    {
        return ESP_OK;
    }
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_SKY_MQTT_BROKER_URI,
        .credentials.username = CONFIG_SKY_MQTT_USERNAME,
        .credentials.client_id = CONFIG_SKY_MQTT_CLIENT_ID,
        .credentials.authentication.password = CONFIG_SKY_MQTT_PASSWORD,
        .session.keepalive = CONFIG_SKY_MQTT_KEEPALIVE_SEC,
        .network.reconnect_timeout_ms = 5000,
        .network.timeout_ms = 10000,
    };
#if CONFIG_SKY_MQTT_USE_CERT_BUNDLE
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#elif CONFIG_SKY_MQTT_SKIP_SERVER_CERT_VERIFY
    mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
#endif
    s_cloud.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_cloud.mqtt_client == NULL)
    {

        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}
/* 销毁 MQTT 客户端并清理连接状态。 */
static void app_cloud_destroy_mqtt_client(void)
{
    if (s_cloud.mqtt_client != NULL)
    {
        esp_mqtt_client_destroy(s_cloud.mqtt_client);
        s_cloud.mqtt_client = NULL;
    }
    s_cloud.mqtt_started = false;
    s_cloud.mqtt_connected = false;
}
/* Wi-Fi 已连上时启动 MQTT 客户端。 */
static esp_err_t app_cloud_start_mqtt_if_needed(void)
{
    if (s_cloud.mqtt_client == NULL && app_cloud_create_mqtt_client() != ESP_OK)
    {
        return ESP_FAIL;
    }
    if (s_cloud.mqtt_client == NULL || s_cloud.mqtt_started)
    {
        return ESP_OK;
    }
    esp_err_t ret = esp_mqtt_client_register_event(s_cloud.mqtt_client,
        ESP_EVENT_ANY_ID,
        app_cloud_mqtt_event_handler,
        NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "register mqtt event failed: %s", esp_err_to_name(ret));
        return ret;
    }

    app_cloud_log_heap("before mqtt start");
    ret = esp_mqtt_client_start(s_cloud.mqtt_client);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "mqtt client start failed: %s", esp_err_to_name(ret));
        app_cloud_destroy_mqtt_client();
        return ret;
    }
    s_cloud.mqtt_started = true;
    ESP_LOGI(TAG, "EMQX mqtt client started");
    return ESP_OK;
}
/* 处理云端 set_target 命令。 */
static esp_err_t app_cloud_receive_set_target(uint16_t target_id)
{
    ESP_LOGI(TAG, "cloud rx: set_target=%u", (unsigned)target_id);
    return app_task_set_target_id(target_id, true);
}
/* 处理云端 start_task 命令，并记录 request_id。 */
static esp_err_t app_cloud_receive_start_task(const app_cloud_cmd_t *cmd)
{
    if (cmd == NULL)
    {

        return ESP_ERR_INVALID_ARG;
    }
    char previous_request_id[sizeof(s_cloud.current_request_id)];
    strlcpy(previous_request_id, s_cloud.current_request_id, sizeof(previous_request_id));
    strlcpy(s_cloud.current_request_id, cmd->request_id, sizeof(s_cloud.current_request_id));
    ESP_LOGI(TAG,
        "cloud rx: start_task target=%u request_id=%s",
        (unsigned)cmd->target_id,
        (cmd->request_id[0] != '\0') ? cmd->request_id : "-");

    esp_err_t ret = app_task_submit_remote_request(cmd->target_id, "emqx");
    if (ret != ESP_OK)
    {
        strlcpy(s_cloud.current_request_id, previous_request_id, sizeof(s_cloud.current_request_id));
    }
    return ret;
}
/* 处理云端 cancel 命令。 */
static esp_err_t app_cloud_receive_cancel(void)
{
    ESP_LOGI(TAG, "cloud rx: cancel");
    app_task_cancel("cancelled by cloud");
    return ESP_OK;
}
/* 解析并分发云端 MQTT 命令 payload。 */
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
    esp_err_t ret = app_cloud_parse_command_json(json, &cmd);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "bad EMQX cmd payload, len=%u", (unsigned)copy_len);
        return ret;
    }
    ESP_LOGD(TAG,
        "EMQX cmd rx cmd=%s target=%u request_id=%s",
        cmd.cmd,
        (unsigned)cmd.target_id,
        (cmd.request_id[0] != '\0') ? cmd.request_id : "-");
    if (strcmp(cmd.cmd, "start_task") == 0)
    {
        ret = app_cloud_receive_start_task(&cmd);

        (void)app_cloud_publish_ack(&cmd,
            (ret == ESP_OK) ? 0 : -1,
            (ret == ESP_OK) ? "accepted" : "start_failed",
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

/* 将 MQTT 事件中的非零结尾片段复制成 C 字符串。 */
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

/* -------------------------------------------------------------------------- */
/* MQTT 和 Wi-Fi 事件回调                                              */
/* -------------------------------------------------------------------------- */

/* 处理 MQTT DATA 事件，筛选命令 topic 并解析命令。 */
static void app_cloud_handle_mqtt_data_event(esp_mqtt_event_handle_t event)
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
    ESP_LOGD(TAG, "mqtt rx topic=%s len=%d", topic, data_len);
    if (strcmp(topic, s_cloud.topic_cmd) == 0)
    {
        (void)app_cloud_handle_command(payload, (size_t)data_len);
    }
}
/* 云端后台任务，等待 Wi-Fi 事件通知后启动 MQTT。 */
static void app_cloud_task(void *arg)
{
    (void)arg;
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_cloud.event_group,
            APP_CLOUD_START_MQTT_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);
        if ((bits & APP_CLOUD_START_MQTT_BIT) != 0)
        {
            ESP_LOGD(TAG, "cloud task: start mqtt");
            esp_err_t ret = app_cloud_start_mqtt_if_needed();
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "defer mqtt start retry: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(2000));
                xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_START_MQTT_BIT);
            }
        }
    }
}
/* Wi-Fi/IP 事件回调，负责联网、重连和触发 MQTT 启动。 */
static void app_cloud_wifi_event_handler(void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT)
    {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGD(TAG, "wifi sta start -> connect");

            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {

                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG,
                    "wifi disconnected -> reconnect, reason=%d",
                    disc ? disc->reason : -1);

                xEventGroupClearBits(s_cloud.event_group,
                    APP_CLOUD_WIFI_CONNECTED_BIT |
                    APP_CLOUD_MQTT_CONNECTED_BIT |
                    APP_CLOUD_START_MQTT_BIT);
                s_cloud.mqtt_connected = false;
                app_cloud_destroy_mqtt_client();

                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            }
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "wifi got ip");

        xEventGroupSetBits(s_cloud.event_group,
            APP_CLOUD_WIFI_CONNECTED_BIT | APP_CLOUD_START_MQTT_BIT);
    }
}
/* MQTT 事件回调，维护连接状态、订阅命令并处理数据。 */
static void app_cloud_mqtt_event_handler(void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "EMQX mqtt connected");
        s_cloud.mqtt_connected = true;

        xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
        {

            int sub_cmd = esp_mqtt_client_subscribe(s_cloud.mqtt_client,
                s_cloud.topic_cmd,
                CONFIG_SKY_MQTT_QOS);
            ESP_LOGD(TAG, "subscribe sent cmd=%d", sub_cmd);
        }
        if (s_cloud.have_last_snapshot)
        {

            app_cloud_publish_task_snapshot_internal(&s_cloud.last_snapshot);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "EMQX mqtt disconnected");
        s_cloud.mqtt_connected = false;

        xEventGroupClearBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGD(TAG, "mqtt subscribed, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "mqtt published, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:

        app_cloud_handle_mqtt_data_event(event);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "mqtt error event");
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

/* 初始化云端链路：配置网络、注册事件、创建任务并启动 Wi-Fi。 */
esp_err_t app_cloud_init(void)
{
    if (s_cloud.inited)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(app_cloud_validate_config(), TAG, "invalid network/mqtt config");

    s_cloud.event_group = xEventGroupCreate();
    if (s_cloud.event_group == NULL)
    {

        return ESP_ERR_NO_MEM;
    }
    app_cloud_build_topics();
    ESP_RETURN_ON_ERROR(app_task_register_event_callback(app_cloud_on_task_event, NULL),
        TAG,
        "register task callback failed");
    ESP_RETURN_ON_ERROR(app_cloud_init_netif_once(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(app_cloud_init_event_loop_once(), TAG, "event loop init failed");
    s_cloud.sta_netif = esp_netif_create_default_wifi_sta();
    if (s_cloud.sta_netif == NULL)
    {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return ESP_FAIL;
    }
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &app_cloud_wifi_event_handler,
        NULL),
        TAG,
        "register wifi event failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &app_cloud_wifi_event_handler,
        NULL),
        TAG,
        "register ip event failed");
    if (s_cloud_task == NULL)
    {

        BaseType_t ok = xTaskCreate(app_cloud_task,
            "app_cloud",
            APP_CLOUD_TASK_STACK_SIZE,
            NULL,
            APP_CLOUD_TASK_PRIORITY,
            &s_cloud_task);
        if (ok != pdPASS)
        {
            ESP_LOGE(TAG, "create app_cloud task failed");

            return ESP_ERR_NO_MEM;
        }
    }
    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid,
        CONFIG_SKY_WIFI_SSID,
        sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password,
        CONFIG_SKY_WIFI_PASSWORD,
        sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.scan_method = WIFI_FAST_SCAN;
    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_cfg.sta.failure_retry_cnt = CONFIG_SKY_WIFI_MAXIMUM_RETRY;
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi set storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "wifi set config failed");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi set ps failed");
    ESP_LOGI(TAG, "wifi started, mqtt waits for ip");
    app_cloud_log_topics_once();
    s_cloud.inited = true;
    ESP_LOGI(TAG, "EMQX init done (official host Wi-Fi path via ESP32-C6)");
    return ESP_OK;
}
