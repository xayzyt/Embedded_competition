/*
 * app_cloud.c - Wi-Fi / MQTT 云端通信模块（详细注释版）
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

#include "app_cloud.h"                             // 项目自定义模块头文件，声明 app_cloud 对外提供的接口。
#include <ctype.h>                                 // 字符判断函数，例如 isprint，用来过滤串口文本字符。
#include <stdbool.h>                               // C99 布尔类型支持，提供 bool、true、false。
#include <stdint.h>                                // 固定宽度整数类型，例如 uint8_t、uint16_t、uint32_t，嵌入式代码常用。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <stdlib.h>                                // 标准库函数，例如 strtol、atoi、内存/数值转换等。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
#include <time.h>                                  // 时间相关定义，云端状态上报可能需要时间戳。
#include "sdkconfig.h"                             // ESP-IDF menuconfig 生成的配置头文件。
#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/event_groups.h"                 // FreeRTOS 事件组，用 bit 标志表示 READY、ACK、MQTT 连接等状态。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include "esp_check.h"                             // ESP-IDF 错误检查宏，例如 ESP_RETURN_ON_FALSE/ESP_GOTO_ON_ERROR。
#include "esp_crt_bundle.h"                        // 内置 CA 证书包，MQTTS/HTTPS 校验证书时会用到。
#include "esp_err.h"                               // ESP-IDF 错误码类型 esp_err_t 和 ESP_OK 等定义。
#include "esp_event.h"                             // ESP-IDF 事件循环，用于 Wi-Fi/IP/MQTT 等异步事件派发。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "esp_netif.h"                             // ESP-IDF 网络接口抽象层，Wi-Fi STA 初始化需要。
#include "esp_wifi.h"                              // ESP-IDF Wi-Fi 驱动接口。
#include "mqtt_client.h"                           // ESP-IDF MQTT 客户端，用于连接 EMQX/云端 broker。
static const char *TAG = "app_cloud";                            // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
#define APP_CLOUD_WIFI_CONNECTED_BIT   BIT0              // 事件组或标志位 bit 定义。
#define APP_CLOUD_MQTT_CONNECTED_BIT   BIT1              // 事件组或标志位 bit 定义。
#define APP_CLOUD_START_MQTT_BIT       BIT2              // 事件组或标志位 bit 定义。
#define APP_CLOUD_TASK_STACK_SIZE      (8 * 1024)        // FreeRTOS 任务栈大小，单位一般是字节。
#define APP_CLOUD_TASK_PRIORITY        5                 // FreeRTOS 任务优先级，数值越大优先级越高。
#define APP_CLOUD_TOPIC_BUF_LEN        192               // MQTT topic 缓冲区或主题相关配置。
#define APP_CLOUD_JSON_BUF_LEN         512               // JSON 字符串缓冲区长度配置。
#define APP_CLOUD_CMD_PAYLOAD_LEN      256               // 命令码或命令字符串相关定义。
#define APP_CLOUD_LOG_AUTH             1                 // 项目级编译期配置宏，用于集中管理参数，避免 magic number 散落在代码里。
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    char cmd[24];
    uint16_t target_id;
    char request_id[32];
} app_cloud_cmd_t;
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    bool inited;
    bool mqtt_started;
    bool mqtt_connected;
    bool have_last_snapshot;
    uint32_t msg_seq;
    EventGroupHandle_t event_group;
    esp_netif_t *sta_netif;
    esp_mqtt_client_handle_t mqtt_client;
    app_task_snapshot_t last_snapshot;
    char current_request_id[32];
    char topic_cmd[APP_CLOUD_TOPIC_BUF_LEN];
    char topic_ack[APP_CLOUD_TOPIC_BUF_LEN];
    char topic_state[APP_CLOUD_TOPIC_BUF_LEN];
} app_cloud_runtime_t;
static app_cloud_runtime_t s_cloud = {0};                        // 模块级静态变量 s_cloud，只在本文件内部使用，避免被其他文件直接修改。
static TaskHandle_t s_cloud_task = NULL;                         // 模块级静态变量 s_cloud_task，只在本文件内部使用，避免被其他文件直接修改。
/*
 * MQTT 和 Wi-Fi 事件回调，更新连接状态并触发订阅/发布。
 */
static void app_cloud_mqtt_event_handler(void *handler_args,
                                         esp_event_base_t base,
                                         int32_t event_id,
                                         void *event_data);
/*
 * 从简单 JSON 字符串中提取指定 key 的字符串值，避免引入较重 JSON 库。
 */
static bool app_cloud_json_get_string(const char *json,
                                      const char *key,
                                      char *out,
                                      size_t out_size)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (json == NULL || key == NULL || out == NULL || out_size == 0U) {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1U < out_size) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        out[i++] = *p++;
    }
    if (*p != '"') {
        return false;
    }
    out[i] = '\0';
    return true;
}
/*
 * 从简单 JSON 字符串中提取 uint16 数值，例如目标 tag ID。
 */
static bool app_cloud_json_get_u16(const char *json,
                                   const char *key,
                                   uint16_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    unsigned value = 0U;
    while (isdigit((unsigned char)*p)) {
        value = value * 10U + (unsigned)(*p - '0');
        if (value > UINT16_MAX) {
            return false;
        }
        p++;
    }
    *out = (uint16_t)value;
    return true;
}
/*
 * 解析云端命令 JSON，得到 cmd、target_id、request_id 等字段。
 */
static esp_err_t app_cloud_parse_command_json(const char *payload, app_cloud_cmd_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (payload == NULL || out == NULL) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!app_cloud_json_get_string(payload, "cmd", out->cmd, sizeof(out->cmd))) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
/*
 * 从简单 JSON 字符串中提取 uint16 数值，例如目标 tag ID。
 */
    (void)app_cloud_json_get_u16(payload, "target_id", &out->target_id);
/*
 * 从简单 JSON 字符串中提取指定 key 的字符串值，避免引入较重 JSON 库。
 */
    (void)app_cloud_json_get_string(payload, "request_id", out->request_id, sizeof(out->request_id));
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 只初始化一次 ESP-IDF 网络接口层，避免重复创建 netif。
 */
static esp_err_t app_cloud_init_netif_once(void)
{
    esp_err_t ret = esp_netif_init();
    if (ret == ESP_ERR_INVALID_STATE) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    return ret;
}
/*
 * 只初始化一次 ESP-IDF 默认事件循环，Wi-Fi/IP 事件依赖它分发。
 */
static esp_err_t app_cloud_init_event_loop_once(void)
{
    esp_err_t ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    return ret;
}
/*
 * 根据设备 ID 生成 MQTT topic，区分命令、ACK 和状态上报主题。
 */
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
/*
 * 启动时打印 topic，方便你在 EMQX 或 MQTTX 里核对订阅/发布路径。
 */
static void app_cloud_log_topics_once(void)
{
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "wifi ssid   : %s", CONFIG_SKY_WIFI_SSID);
    ESP_LOGI(TAG, "mqtt broker : %s", CONFIG_SKY_MQTT_BROKER_URI);
    ESP_LOGI(TAG, "client id   : %s", CONFIG_SKY_MQTT_CLIENT_ID);
    ESP_LOGI(TAG, "device name : %s", CONFIG_SKY_MQTT_DEVICE_NAME);
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "topic cmd   : %s", s_cloud.topic_cmd);
    ESP_LOGI(TAG, "topic ack   : %s", s_cloud.topic_ack);
    ESP_LOGI(TAG, "topic state : %s", s_cloud.topic_state);
    ESP_LOGI(TAG, "cmd example : {\"cmd\":\"start_task\",\"target_id\":3,\"request_id\":\"abc001\"}");
}
/*
 * 检查 Wi-Fi、MQTT broker、设备 ID 等配置是否有效。
 */
static esp_err_t app_cloud_validate_config(void)
{
    if (CONFIG_SKY_WIFI_SSID[0] == '\0') {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "CONFIG_SKY_WIFI_SSID is empty, please set it in menuconfig");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_BROKER_URI[0] == '\0') {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_BROKER_URI is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_CLIENT_ID[0] == '\0') {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_CLIENT_ID is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_DEVICE_NAME[0] == '\0') {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_DEVICE_NAME is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (CONFIG_SKY_MQTT_TOPIC_PREFIX[0] == '\0') {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_TOPIC_PREFIX is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(CONFIG_SKY_WIFI_SSID) >= sizeof(((wifi_config_t *)0)->sta.ssid)) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "Wi-Fi SSID too long");
        return ESP_ERR_INVALID_SIZE;
    }
    if (strlen(CONFIG_SKY_WIFI_PASSWORD) >= sizeof(((wifi_config_t *)0)->sta.password)) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "Wi-Fi password too long");
        return ESP_ERR_INVALID_SIZE;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 底层 MQTT 发布封装，统一处理连接检查、topic/payload 参数和日志输出。
 */
static esp_err_t app_cloud_publish_raw(const char *topic,
                                       const char *payload,
                                       int qos,
                                       int retain)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (topic == NULL || payload == NULL) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.mqtt_client == NULL || !s_cloud.mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    // 向 MQTT broker 发布消息，通常用于状态上报或命令 ACK。
    int msg_id = esp_mqtt_client_publish(s_cloud.mqtt_client, topic, payload, 0, qos, retain);
    if (msg_id < 0) {
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "mqtt publish failed, topic=%s", topic);
        return ESP_FAIL;
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "mqtt tx topic=%s msg_id=%d payload=%s", topic, msg_id, payload);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 把云端命令处理结果组织成 ACK JSON，发布回 ack topic。
 */
static esp_err_t app_cloud_publish_ack(const app_cloud_cmd_t *cmd,
                                       int code,
                                       const char *msg,
                                       uint16_t target_id)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (cmd == NULL) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
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
/*
 * 把任务快照组织成 JSON 并发布到状态 topic。
 */
static void app_cloud_publish_task_snapshot_internal(const app_task_snapshot_t *snap)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (snap == NULL) {
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
    if (ret != ESP_OK) {
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "task snapshot not sent yet: %s", esp_err_to_name(ret));
    }
}
/*
 * 任务状态变化回调，缓存最新快照并尝试立即上报云端。
 */
static void app_cloud_on_task_event(app_task_event_t event,
                                    const app_task_snapshot_t *snap,
                                    void *user_ctx)
{
    (void)event;
    (void)user_ctx;
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (snap == NULL) {
        return;
    }
    s_cloud.last_snapshot = *snap;
    s_cloud.have_last_snapshot = true;
    app_cloud_publish_task_snapshot_internal(snap);
}
/*
 * 创建并配置 MQTT 客户端，注册事件回调。
 */
static esp_err_t app_cloud_create_mqtt_client(void)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.mqtt_client != NULL) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
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
#if APP_CLOUD_LOG_AUTH
    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
    ESP_LOGW(TAG, "==== EMQX mqtt auth debug begin ====");
    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
    ESP_LOGW(TAG, "debug mqtt username : %s", mqtt_cfg.credentials.username);
    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
    ESP_LOGW(TAG, "debug mqtt client_id: %s", mqtt_cfg.credentials.client_id);
    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
    ESP_LOGW(TAG, "debug mqtt password : %s", mqtt_cfg.credentials.authentication.password);
    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
    ESP_LOGW(TAG, "==== EMQX mqtt auth debug end ====");
#endif
    // 创建 MQTT 客户端对象并写入 broker、认证等配置。
    s_cloud.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.mqtt_client == NULL) {
        // 创建 MQTT 客户端对象并写入 broker、认证等配置。
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 销毁 MQTT 客户端，断开云端连接并释放资源。
 */
static void app_cloud_destroy_mqtt_client(void)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.mqtt_client != NULL) {
        esp_mqtt_client_destroy(s_cloud.mqtt_client);
        s_cloud.mqtt_client = NULL;
    }
    s_cloud.mqtt_started = false;
    s_cloud.mqtt_connected = false;
}
/*
 * 当 Wi-Fi 已连接且 MQTT 未启动时启动 MQTT 客户端。
 */
static void app_cloud_start_mqtt_if_needed(void)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.mqtt_client == NULL && app_cloud_create_mqtt_client() != ESP_OK) {
        return;
    }
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.mqtt_client == NULL || s_cloud.mqtt_started) {
        return;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_cloud.mqtt_client,
                                                   ESP_EVENT_ANY_ID,
                                                   app_cloud_mqtt_event_handler,
                                                   NULL));
    // 启动 MQTT 客户端连接流程。
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_cloud.mqtt_client));
    s_cloud.mqtt_started = true;
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "EMQX mqtt client started");
}
/*
 * 处理云端设置目标 tag ID 的命令。
 */
static esp_err_t app_cloud_receive_set_target(uint16_t target_id)
{
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "cloud rx: set_target=%u", (unsigned)target_id);
    return app_task_set_target_id(target_id, true);
}
/*
 * 处理云端开始接驳任务的命令。
 */
static esp_err_t app_cloud_receive_start_task(const app_cloud_cmd_t *cmd)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (cmd == NULL) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
    char previous_request_id[sizeof(s_cloud.current_request_id)];
    strlcpy(previous_request_id, s_cloud.current_request_id, sizeof(previous_request_id));
    strlcpy(s_cloud.current_request_id, cmd->request_id, sizeof(s_cloud.current_request_id));
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG,
             "cloud rx: start_task target=%u request_id=%s",
             (unsigned)cmd->target_id,
             (cmd->request_id[0] != '\0') ? cmd->request_id : "-");
    esp_err_t ret = app_task_submit_remote_request(cmd->target_id, "emqx");
    if (ret != ESP_OK) {
        strlcpy(s_cloud.current_request_id, previous_request_id, sizeof(s_cloud.current_request_id));
    }
    return ret;
}
/*
 * 处理云端取消当前任务的命令。
 */
static esp_err_t app_cloud_receive_cancel(void)
{
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "cloud rx: cancel");
    app_task_cancel("cancelled by cloud");
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 统一处理 MQTT 收到的命令 payload，并调用 app_task 执行业务动作。
 */
static esp_err_t app_cloud_handle_command(const char *payload, size_t payload_len)
{
    char json[APP_CLOUD_CMD_PAYLOAD_LEN];
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (payload == NULL || payload_len == 0U) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
    size_t copy_len = payload_len;
    if (copy_len >= sizeof(json)) {
        copy_len = sizeof(json) - 1U;
    }
    memcpy(json, payload, copy_len);
    json[copy_len] = '\0';
    app_cloud_cmd_t cmd = {0};
    esp_err_t ret = app_cloud_parse_command_json(json, &cmd);
    if (ret != ESP_OK) {
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "bad EMQX cmd payload: %s", json);
        return ret;
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG,
             "EMQX cmd rx cmd=%s target=%u request_id=%s",
             cmd.cmd,
             (unsigned)cmd.target_id,
             (cmd.request_id[0] != '\0') ? cmd.request_id : "-");
    if (strcmp(cmd.cmd, "start_task") == 0) {
        ret = app_cloud_receive_start_task(&cmd);
/*
 * 把云端命令处理结果组织成 ACK JSON，发布回 ack topic。
 */
        (void)app_cloud_publish_ack(&cmd,
                                    (ret == ESP_OK) ? 0 : -1,
                                    (ret == ESP_OK) ? "accepted" : "start_failed",
                                    cmd.target_id);
        return ret;
    }
    if (strcmp(cmd.cmd, "set_target") == 0) {
        ret = app_cloud_receive_set_target(cmd.target_id);
/*
 * 把云端命令处理结果组织成 ACK JSON，发布回 ack topic。
 */
        (void)app_cloud_publish_ack(&cmd,
                                    (ret == ESP_OK) ? 0 : -1,
                                    (ret == ESP_OK) ? "accepted" : "set_failed",
                                    cmd.target_id);
        return ret;
    }
    if (strcmp(cmd.cmd, "cancel") == 0) {
        ret = app_cloud_receive_cancel();
/*
 * 把云端命令处理结果组织成 ACK JSON，发布回 ack topic。
 */
        (void)app_cloud_publish_ack(&cmd,
                                    (ret == ESP_OK) ? 0 : -1,
                                    (ret == ESP_OK) ? "accepted" : "cancel_failed",
                                    0U);
        return ret;
    }
    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
    ESP_LOGW(TAG, "unknown cmd=%s", cmd.cmd);
/*
 * 把云端命令处理结果组织成 ACK JSON，发布回 ack topic。
 */
    (void)app_cloud_publish_ack(&cmd, -2, "unknown_cmd", cmd.target_id);
    return ESP_ERR_NOT_SUPPORTED;
}
/*
 * 处理 MQTT DATA 事件，从 topic/payload 中取出云端下发内容。
 */
static void app_cloud_handle_mqtt_data_event(esp_mqtt_event_handle_t event)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (event == NULL || event->topic == NULL || event->data == NULL) {
        return;
    }
    char topic[APP_CLOUD_TOPIC_BUF_LEN];
    char payload[APP_CLOUD_JSON_BUF_LEN];
    int topic_len = event->topic_len;
    int data_len = event->data_len;
    if (topic_len <= 0) {
        return;
    }
    if ((size_t)topic_len >= sizeof(topic)) {
        topic_len = sizeof(topic) - 1;
    }
    memcpy(topic, event->topic, (size_t)topic_len);
    topic[topic_len] = '\0';
    if (data_len < 0) {
        data_len = 0;
    }
    if ((size_t)data_len >= sizeof(payload)) {
        data_len = sizeof(payload) - 1;
    }
    if (data_len > 0) {
        memcpy(payload, event->data, (size_t)data_len);
    }
    payload[data_len] = '\0';
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "mqtt rx topic=%s payload=%s", topic, payload);
    if (strcmp(topic, s_cloud.topic_cmd) == 0) {
/*
 * 统一处理 MQTT 收到的命令 payload，并调用 app_task 执行业务动作。
 */
        (void)app_cloud_handle_command(payload, (size_t)data_len);
    }
}
/*
 * 云端后台任务，负责 Wi-Fi/MQTT 状态维护和首次状态发布。
 */
static void app_cloud_task(void *arg)
{
    (void)arg;
    while (1) {
        // 等待事件组 bit，适合 ACK/READY/MQTT 连接这类同步点。
        EventBits_t bits = xEventGroupWaitBits(s_cloud.event_group,
                                               APP_CLOUD_START_MQTT_BIT,
                                               pdTRUE,
                                               pdFALSE,
                                               portMAX_DELAY);
        if ((bits & APP_CLOUD_START_MQTT_BIT) != 0) {
            // 信息日志：用于确认程序执行到了哪个阶段。
            ESP_LOGI(TAG, "cloud task: start mqtt");
            app_cloud_start_mqtt_if_needed();
        }
    }
}
/*
 * Wi-Fi/IP 事件回调，负责处理连接成功、断开、获得 IP 等网络状态。
 */
static void app_cloud_wifi_event_handler(void *arg,
                                         esp_event_base_t event_base,
                                         int32_t event_id,
                                         void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                // 信息日志：用于确认程序执行到了哪个阶段。
                ESP_LOGI(TAG, "wifi sta start -> connect");
                // 发起 Wi-Fi STA 连接。
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
                // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
                ESP_LOGW(TAG,
                         "wifi disconnected -> reconnect, reason=%d",
                         disc ? disc->reason : -1);
                // 清除事件组 bit，避免旧事件影响下一次等待。
                xEventGroupClearBits(s_cloud.event_group,
                                     APP_CLOUD_WIFI_CONNECTED_BIT |
                                     APP_CLOUD_MQTT_CONNECTED_BIT |
                                     APP_CLOUD_START_MQTT_BIT);
                s_cloud.mqtt_connected = false;
                app_cloud_destroy_mqtt_client();
                // 发起 Wi-Fi STA 连接。
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // 信息日志：用于确认程序执行到了哪个阶段。
        ESP_LOGI(TAG, "wifi got ip");
        // 置位事件组 bit，用来通知其他任务某个事件已经发生。
        xEventGroupSetBits(s_cloud.event_group,
                           APP_CLOUD_WIFI_CONNECTED_BIT | APP_CLOUD_START_MQTT_BIT);
    }
}
/*
 * MQTT 和 Wi-Fi 事件回调，更新连接状态并触发订阅/发布。
 */
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
            // 信息日志：用于确认程序执行到了哪个阶段。
            ESP_LOGI(TAG, "EMQX mqtt connected");
            s_cloud.mqtt_connected = true;
            // 置位事件组 bit，用来通知其他任务某个事件已经发生。
            xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
            {
                // 订阅云端命令 topic，让设备能收到小程序/服务器下发的控制命令。
                int sub_cmd = esp_mqtt_client_subscribe(s_cloud.mqtt_client,
                                                        s_cloud.topic_cmd,
                                                        CONFIG_SKY_MQTT_QOS);
                // 信息日志：用于确认程序执行到了哪个阶段。
                ESP_LOGI(TAG, "subscribe sent cmd=%d", sub_cmd);
            }
            if (s_cloud.have_last_snapshot) {
                app_cloud_publish_task_snapshot_internal(&s_cloud.last_snapshot);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
            ESP_LOGW(TAG, "EMQX mqtt disconnected");
            s_cloud.mqtt_connected = false;
            // 清除事件组 bit，避免旧事件影响下一次等待。
            xEventGroupClearBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            // 信息日志：用于确认程序执行到了哪个阶段。
            ESP_LOGI(TAG, "mqtt subscribed, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "mqtt published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            app_cloud_handle_mqtt_data_event(event);
            break;
        case MQTT_EVENT_ERROR:
            // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
            ESP_LOGW(TAG, "mqtt error event");
            break;
        default:
            break;
    }
}
/*
 * 初始化云端模块，创建事件组、网络、Wi-Fi、MQTT 和后台任务。
 */
esp_err_t app_cloud_init(void)
{
    if (s_cloud.inited) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(app_cloud_validate_config(), TAG, "invalid network/mqtt config");
    // 创建事件组，用多个 bit 表示异步状态。
    s_cloud.event_group = xEventGroupCreate();
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.event_group == NULL) {
        // 内存不足是嵌入式项目常见问题，这里返回错误让上层决定是否停止初始化。
        return ESP_ERR_NO_MEM;
    }
    app_cloud_build_topics();
    ESP_RETURN_ON_ERROR(app_task_register_event_callback(app_cloud_on_task_event, NULL),
                        TAG,
                        "register task callback failed");
    ESP_RETURN_ON_ERROR(app_cloud_init_netif_once(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(app_cloud_init_event_loop_once(), TAG, "event loop init failed");
    s_cloud.sta_netif = esp_netif_create_default_wifi_sta();
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud.sta_netif == NULL) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return ESP_FAIL;
    }
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "esp_wifi_init failed");
    // 注册 ESP-IDF 事件回调，用异步方式处理 Wi-Fi/IP/MQTT 状态变化。
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   &app_cloud_wifi_event_handler,
                                                   NULL),
                        TAG,
                        "register wifi event failed");
    // 注册 ESP-IDF 事件回调，用异步方式处理 Wi-Fi/IP/MQTT 状态变化。
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,
                                                   IP_EVENT_STA_GOT_IP,
                                                   &app_cloud_wifi_event_handler,
                                                   NULL),
                        TAG,
                        "register ip event failed");
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_cloud_task == NULL) {
        // 创建 FreeRTOS 后台任务，把耗时逻辑从主流程中拆出去。
        BaseType_t ok = xTaskCreate(app_cloud_task,
                                    "app_cloud",
                                    APP_CLOUD_TASK_STACK_SIZE,
                                    NULL,
                                    APP_CLOUD_TASK_PRIORITY,
                                    &s_cloud_task);
        if (ok != pdPASS) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "create app_cloud task failed");
            // 内存不足是嵌入式项目常见问题，这里返回错误让上层决定是否停止初始化。
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
    // 启动 Wi-Fi 驱动，开始连接 AP。
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi set ps failed");
    app_cloud_log_topics_once();
    s_cloud.inited = true;
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "EMQX init done (official host Wi-Fi path via ESP32-C6)");
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 对外发布任务快照；如果 MQTT 暂未连接，会先缓存最新快照。
 */
void app_cloud_publish_task_snapshot(const app_task_snapshot_t *snap)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (snap == NULL) {
        return;
    }
    s_cloud.last_snapshot = *snap;
    s_cloud.have_last_snapshot = true;
    app_cloud_publish_task_snapshot_internal(snap);
}
