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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_hosted.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "app_ch32_link.h"
#include "app_cloud_cmd.h"
#include "app_task.h"
#include "app_ui.h"

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
#define APP_CLOUD_FALLBACK_DNS_A       223
#define APP_CLOUD_FALLBACK_DNS_B       5
#define APP_CLOUD_FALLBACK_DNS_C       5
#define APP_CLOUD_FALLBACK_DNS_D       5
#define APP_CLOUD_NTP_SERVER           "ntp.aliyun.com"
#define APP_CLOUD_TIMEZONE             "CST-8"
#define APP_CLOUD_WEATHER_CITY         "changsha"
#define APP_CLOUD_WEATHER_CITY_CN      "长沙"
#define APP_CLOUD_WEATHER_URL_LEN      256
#define APP_CLOUD_WEATHER_RESP_LEN     1024
#define APP_CLOUD_WEATHER_TASK_STACK   (6 * 1024)
#define APP_CLOUD_WEATHER_TASK_PRIO    4
#define APP_CLOUD_WEATHER_RETRY_MS     (30 * 1000)
#define APP_CLOUD_WEATHER_REFRESH_MS   (30 * 60 * 1000)
#define APP_CLOUD_WEATHER_FALLBACK_CODE 4
#define APP_CLOUD_WEATHER_SEVERE_CODE  36
#define APP_CLOUD_ABORT_WAIT_MS        800

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool inited;                                      /* 云端模块是否完成初始化。 */
    bool mqtt_started;                                /* MQTT 客户端是否已经启动。 */
    bool mqtt_connected;                              /* MQTT 当前是否已连接。 */
    bool sntp_started;                                /* SNTP 时间同步是否已经启动。 */
    bool weather_simulated;                           /* 是否启用恶劣天气模拟。 */
    bool weather_docking_blocked;                     /* 当前天气是否禁止接驳。 */
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
static TaskHandle_t s_weather_task = NULL;

/* MQTT 事件处理函数需要先声明，创建客户端时会注册它。 */
static void app_cloud_mqtt_event_handler(void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data);
static esp_err_t app_cloud_publish_raw(const char *topic,
    const char *payload,
    int qos,
    int retain);

typedef struct {
    char *buf;
    int len;
    int cap;
    bool truncated;
} app_cloud_http_buf_t;

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

static bool app_cloud_json_add_string(cJSON *root, const char *key, const char *value)
{
    return cJSON_AddStringToObject(root, key, (value != NULL) ? value : "") != NULL;
}

static bool app_cloud_json_add_number(cJSON *root, const char *key, double value)
{
    return cJSON_AddNumberToObject(root, key, value) != NULL;
}

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
static void app_cloud_request_wifi_connect(const char *reason)
{
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG,
            "wifi connect request failed (%s): %s",
            (reason != NULL) ? reason : "-",
            esp_err_to_name(ret));
    }
}

static void app_cloud_log_dns_info(const char *label, const esp_netif_dns_info_t *dns)
{
    if (dns == NULL || dns->ip.u_addr.ip4.addr == 0)
    {
        ESP_LOGW(TAG, "%s dns: not configured", label);
        return;
    }

    char ip[16] = {0};
    ESP_LOGI(TAG,
        "%s dns: %s",
        label,
        esp_ip4addr_ntoa(&dns->ip.u_addr.ip4, ip, sizeof(ip)));
}

static void app_cloud_ensure_dns_after_ip(void)
{
    if (s_cloud.sta_netif == NULL)
    {
        return;
    }

    esp_netif_dns_info_t dns = {0};
    esp_err_t ret = esp_netif_get_dns_info(s_cloud.sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (ret == ESP_OK && dns.ip.u_addr.ip4.addr != 0)
    {
        app_cloud_log_dns_info("main", &dns);
        return;
    }

    ESP_LOGW(TAG,
        "main dns missing (%s), setting fallback %u.%u.%u.%u",
        esp_err_to_name(ret),
        APP_CLOUD_FALLBACK_DNS_A,
        APP_CLOUD_FALLBACK_DNS_B,
        APP_CLOUD_FALLBACK_DNS_C,
        APP_CLOUD_FALLBACK_DNS_D);

    esp_netif_dns_info_t fallback = {0};
    fallback.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_ip4_addr(&fallback.ip.u_addr.ip4,
        APP_CLOUD_FALLBACK_DNS_A,
        APP_CLOUD_FALLBACK_DNS_B,
        APP_CLOUD_FALLBACK_DNS_C,
        APP_CLOUD_FALLBACK_DNS_D);
    ret = esp_netif_set_dns_info(s_cloud.sta_netif, ESP_NETIF_DNS_MAIN, &fallback);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "set fallback dns failed: %s", esp_err_to_name(ret));
        return;
    }
    app_cloud_log_dns_info("fallback", &fallback);
}

static void app_cloud_time_sync_cb(struct timeval *tv)
{
    (void)tv;

    time_t now = 0;
    time(&now);

    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_buf[32] = {0};
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    ESP_LOGI(TAG, "time synced: %s", time_buf);
}

static void app_cloud_start_sntp_once(void)
{
    if (s_cloud.sntp_started)
    {
        return;
    }

    setenv("TZ", APP_CLOUD_TIMEZONE, 1);
    tzset();

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_CLOUD_NTP_SERVER);
    sntp_config.sync_cb = app_cloud_time_sync_cb;

    esp_err_t ret = esp_netif_sntp_init(&sntp_config);
    if (ret == ESP_ERR_INVALID_STATE)
    {
        s_cloud.sntp_started = true;
        ESP_LOGW(TAG, "SNTP already initialized");
        return;
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(ret));
        return;
    }

    s_cloud.sntp_started = true;
    ESP_LOGI(TAG, "SNTP started, server=%s", APP_CLOUD_NTP_SERVER);
}

static esp_err_t app_cloud_weather_http_event_cb(esp_http_client_event_t *evt)
{
    if (evt == NULL || evt->event_id != HTTP_EVENT_ON_DATA)
    {
        return ESP_OK;
    }

    app_cloud_http_buf_t *rx = (app_cloud_http_buf_t *)evt->user_data;
    if (rx == NULL || rx->buf == NULL || rx->cap <= 0 || evt->data == NULL || evt->data_len <= 0)
    {
        return ESP_OK;
    }

    int free_len = rx->cap - rx->len;
    if (free_len <= 0)
    {
        rx->truncated = true;
        return ESP_OK;
    }

    int copy_len = evt->data_len;
    if (copy_len > free_len)
    {
        copy_len = free_len;
        rx->truncated = true;
    }
    memcpy(rx->buf + rx->len, evt->data, (size_t)copy_len);
    rx->len += copy_len;
    rx->buf[rx->len] = '\0';
    return ESP_OK;
}

static const char *app_cloud_json_get_string(cJSON *obj, const char *key)
{
    if (obj == NULL || key == NULL)
    {
        return NULL;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        return item->valuestring;
    }
    return NULL;
}

static esp_err_t app_cloud_parse_weather_response(const char *json,
    char *weather_text,
    size_t weather_text_size,
    int *weather_code)
{
    if (json == NULL || weather_text == NULL || weather_text_size == 0 || weather_code == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t ret = ESP_ERR_INVALID_RESPONSE;
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    cJSON *first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : NULL;
    cJSON *now = first ? cJSON_GetObjectItemCaseSensitive(first, "now") : NULL;

    const char *text = app_cloud_json_get_string(now, "text");
    const char *code = app_cloud_json_get_string(now, "code");
    const char *temp = app_cloud_json_get_string(now, "temperature");
    if (text != NULL && code != NULL && temp != NULL)
    {
        *weather_code = atoi(code);
        snprintf(weather_text,
            weather_text_size,
            "%s\n%s℃",
            text,
            temp);
        ret = ESP_OK;
    }

    cJSON_Delete(root);
    return ret;
}

static void app_cloud_show_weather_fallback(void)
{
    app_ui_main_screen_set_weather("多云\n--℃", APP_CLOUD_WEATHER_FALLBACK_CODE);
}

static void app_cloud_apply_weather_docking_policy(bool simulated)
{
    s_cloud.weather_docking_blocked = simulated;

    if (!simulated)
    {
        return;
    }

    app_task_cancel("blocked by simulated severe weather");
    app_ui_main_screen_set_task_text("weather blocked");

    if (app_ch32_link_is_ready())
    {
        esp_err_t ret = app_ch32_link_send_cmd_and_wait_ack('S', APP_CLOUD_ABORT_WAIT_MS);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "abort dock for severe weather failed: %s", esp_err_to_name(ret));
        }
    }
}

static void app_cloud_show_simulated_severe_weather(void)
{
    app_ui_main_screen_set_weather("台风\n28℃", APP_CLOUD_WEATHER_SEVERE_CODE);
    app_cloud_apply_weather_docking_policy(true);
}

static esp_err_t app_cloud_fetch_weather_once(void)
{
    if (s_cloud.weather_simulated)
    {
        app_cloud_show_simulated_severe_weather();
        return ESP_OK;
    }

    if (CONFIG_SKY_WEATHER_API_KEY[0] == '\0')
    {
        ESP_LOGW(TAG, "CONFIG_SKY_WEATHER_API_KEY is empty, show local weather fallback");
        app_cloud_show_weather_fallback();
        s_cloud.weather_docking_blocked = false;
        return ESP_OK;
    }

    char url[APP_CLOUD_WEATHER_URL_LEN] = {0};
    snprintf(url,
        sizeof(url),
        "https://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c",
        CONFIG_SKY_WEATHER_API_KEY,
        APP_CLOUD_WEATHER_CITY);

    char response[APP_CLOUD_WEATHER_RESP_LEN] = {0};
    app_cloud_http_buf_t rx = {
        .buf = response,
        .len = 0,
        .cap = (int)sizeof(response) - 1,
        .truncated = false,
    };
    esp_http_client_config_t http_cfg = {
        .url = url,
        .event_handler = app_cloud_weather_http_event_cb,
        .user_data = &rx,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        .timeout_ms = 10000,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL)
    {
        app_ui_main_screen_set_weather("同步失败", 99);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status != 200 || rx.len <= 0 || rx.truncated)
    {
        ESP_LOGW(TAG,
            "weather fetch failed: ret=%s http=%d len=%d trunc=%d",
            esp_err_to_name(ret),
            status,
            rx.len,
            rx.truncated);
        app_ui_main_screen_set_weather("同步失败", 99);
        return (ret != ESP_OK) ? ret : ESP_FAIL;
    }

    char weather_text[96] = {0};
    int weather_code = 99;
    ret = app_cloud_parse_weather_response(response,
        weather_text,
        sizeof(weather_text),
        &weather_code);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "weather json parse failed: %s", esp_err_to_name(ret));
        app_ui_main_screen_set_weather("同步失败", 99);
        return ret;
    }

    s_cloud.weather_docking_blocked = false;
    app_ui_main_screen_set_weather(weather_text, weather_code);
    ESP_LOGI(TAG, "weather updated: %s code=%d", weather_text, weather_code);
    return ESP_OK;
}

static void app_cloud_weather_task(void *arg)
{
    (void)arg;
    app_ui_main_screen_set_weather("同步中", 99);

    for (;;)
    {
        if (!app_cloud_is_wifi_connected())
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        esp_err_t ret = app_cloud_fetch_weather_once();
        vTaskDelay(pdMS_TO_TICKS((ret == ESP_OK) ?
            APP_CLOUD_WEATHER_REFRESH_MS :
            APP_CLOUD_WEATHER_RETRY_MS));
    }
}

static void app_cloud_start_weather_task_once(void)
{
    if (s_weather_task != NULL)
    {
        return;
    }

    BaseType_t ok = xTaskCreate(app_cloud_weather_task,
        "weather",
        APP_CLOUD_WEATHER_TASK_STACK,
        NULL,
        APP_CLOUD_WEATHER_TASK_PRIO,
        &s_weather_task);
    if (ok != pdPASS)
    {
        ESP_LOGW(TAG, "create weather task failed");
        s_weather_task = NULL;
    }
}

void app_cloud_simulate_severe_weather(void)
{
    ESP_LOGW(TAG, "simulate severe weather: block docking");
    s_cloud.weather_simulated = true;
    app_cloud_show_simulated_severe_weather();
}

bool app_cloud_is_weather_docking_blocked(void)
{
    return s_cloud.weather_docking_blocked;
}

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
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    bool ok = app_cloud_json_add_string(root, "request_id", cmd->request_id) &&
        app_cloud_json_add_string(root, "cmd", cmd->cmd) &&
        app_cloud_json_add_number(root, "code", code) &&
        app_cloud_json_add_string(root, "msg", (msg != NULL) ? msg : "-") &&
        app_cloud_json_add_number(root, "target_id", target_id);
    esp_err_t ret = ok ? app_cloud_publish_json(s_cloud.topic_ack, root, 0) : ESP_ERR_NO_MEM;
    cJSON_Delete(root);
    return ret;
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
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGW(TAG, "task snapshot not sent yet: no memory");
        return;
    }

    bool ok = app_cloud_json_add_number(root, "msg_id", seq) &&
        app_cloud_json_add_number(root, "ts", (double)now) &&
        app_cloud_json_add_string(root, "request_id", s_cloud.current_request_id) &&
        app_cloud_json_add_string(root, "device", CONFIG_SKY_MQTT_DEVICE_NAME) &&
        app_cloud_json_add_string(root, "state", app_task_state_to_text(snap->state)) &&
        app_cloud_json_add_number(root, "active", snap->active ? 1U : 0U) &&
        app_cloud_json_add_number(root, "target_id", snap->target_id) &&
        app_cloud_json_add_number(root, "matched_tag_id", snap->matched_tag_id) &&
        app_cloud_json_add_number(root, "cargo_received", cargo_received) &&
        app_cloud_json_add_number(root, "fault", fault) &&
        app_cloud_json_add_string(root, "source", snap->source) &&
        app_cloud_json_add_string(root, "note", snap->note);

    esp_err_t ret = ok ? app_cloud_publish_json(s_cloud.topic_state, root, 1) : ESP_ERR_NO_MEM;
    cJSON_Delete(root);
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
    esp_err_t ret = app_cloud_cmd_parse_json(json, &cmd);
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

            app_cloud_request_wifi_connect("sta_start");
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

                app_cloud_request_wifi_connect("sta_disconnected");
                break;
            }
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "wifi got ip");
        app_cloud_ensure_dns_after_ip();
        app_cloud_start_sntp_once();
        app_cloud_start_weather_task_once();

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
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init failed");
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

bool app_cloud_is_wifi_connected(void)
{
    if (!s_cloud.inited || s_cloud.event_group == NULL)
    {
        return false;
    }
    return (xEventGroupGetBits(s_cloud.event_group) & APP_CLOUD_WIFI_CONNECTED_BIT) != 0;
}

bool app_cloud_is_mqtt_connected(void)
{
    return s_cloud.inited && s_cloud.mqtt_connected;
}
