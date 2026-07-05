#include "app_cloud.h"
#include "app_cloud_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_hosted.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "app_delivery_photo.h"

// 云端连接模块：初始化 ESP-Hosted Wi-Fi、SNTP 和 MQTT，并把任务状态同步到云端。

static const char *TAG = "app_cloud";
app_cloud_runtime_t s_cloud = {0};
static TaskHandle_t s_cloud_task = NULL;
static void app_cloud_mqtt_event_handler(void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data);

/* ---------- ESP-IDF 网络基础设施 ---------- */

// ESP-IDF 全局网络栈/事件循环可能被别的模块初始化过，重复调用视为成功。
static esp_err_t app_cloud_init_netif_once(void)
{
    esp_err_t ret = esp_netif_init();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        return ESP_OK;
    }
    return ret;
}
static esp_err_t app_cloud_init_event_loop_once(void)
{
    esp_err_t ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        return ESP_OK;
    }
    return ret;
}
// 根据 topic 前缀和设备名生成 cmd/ack/state 三类 MQTT 主题。
/* ---------- Wi-Fi、DNS 与时间同步 ---------- */

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
// 统一封装 Wi-Fi 重连请求，保留触发原因方便排查。
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
static esp_err_t app_cloud_set_fallback_dns(esp_netif_dns_type_t type)
{
    esp_netif_dns_info_t fallback = {0};
    fallback.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_ip4_addr(&fallback.ip.u_addr.ip4,
        APP_CLOUD_FALLBACK_DNS_A,
        APP_CLOUD_FALLBACK_DNS_B,
        APP_CLOUD_FALLBACK_DNS_C,
        APP_CLOUD_FALLBACK_DNS_D);
    return esp_netif_set_dns_info(s_cloud.sta_netif, type, &fallback);
}
// 某些热点会给出不可用 DNS；保留 DHCP 主 DNS，同时补一个备份 DNS。
static void app_cloud_ensure_dns_after_ip(void)
{
    if (s_cloud.sta_netif == NULL)
    {
        return;
    }
    esp_netif_dns_info_t dns = {0};
    esp_err_t ret = esp_netif_get_dns_info(s_cloud.sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (ret != ESP_OK || dns.ip.u_addr.ip4.addr == 0)
    {
        ESP_LOGW(TAG,
            "main dns missing (%s), setting fallback %u.%u.%u.%u",
            esp_err_to_name(ret),
            APP_CLOUD_FALLBACK_DNS_A,
            APP_CLOUD_FALLBACK_DNS_B,
            APP_CLOUD_FALLBACK_DNS_C,
            APP_CLOUD_FALLBACK_DNS_D);
        ret = app_cloud_set_fallback_dns(ESP_NETIF_DNS_MAIN);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "set main fallback dns failed: %s", esp_err_to_name(ret));
        }
        return;
    }

    esp_netif_dns_info_t backup = {0};
    ret = esp_netif_get_dns_info(s_cloud.sta_netif, ESP_NETIF_DNS_BACKUP, &backup);
    if (ret == ESP_OK && backup.ip.u_addr.ip4.addr != 0)
    {
        return;
    }
    ret = app_cloud_set_fallback_dns(ESP_NETIF_DNS_BACKUP);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "set backup fallback dns failed: %s", esp_err_to_name(ret));
    }
}
// Wi-Fi 拿到 IP 后启动 SNTP；时区采用中国标准时间。
static void app_cloud_start_sntp_once(void)
{
    if (s_cloud.sntp_started)
    {
        return;
    }
    setenv("TZ", APP_CLOUD_TIMEZONE, 1);
    tzset();
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_CLOUD_NTP_SERVER);
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
}
// 检查 menuconfig 中必须填写的 Wi-Fi/MQTT 参数。
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
// 创建 MQTT 客户端，证书校验策略由 menuconfig 控制。
/* ---------- MQTT 客户端生命周期 ---------- */

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
        .buffer.size = 16 * 1024,
        .buffer.out_size = 16 * 1024,
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
// MQTT 只在 Wi-Fi 已拿到 IP 后启动，失败时由 cloud task 延迟重试。
static esp_err_t app_cloud_start_mqtt_if_needed(void)
{
    if (s_cloud.mqtt_client == NULL && app_cloud_create_mqtt_client() != ESP_OK)
    {
        return ESP_FAIL;
    }
    if (s_cloud.mqtt_client == NULL)
    {
        return ESP_OK;
    }
    if (s_cloud.mqtt_started)
    {
        if (!s_cloud.mqtt_connected)
        {
            esp_err_t ret = esp_mqtt_client_reconnect(s_cloud.mqtt_client);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
            {
                ESP_LOGW(TAG, "mqtt reconnect request failed: %s", esp_err_to_name(ret));
                return ret;
            }
        }
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
    ret = esp_mqtt_client_start(s_cloud.mqtt_client);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "mqtt client start failed: %s", esp_err_to_name(ret));
        app_cloud_destroy_mqtt_client();
        return ret;
    }
    s_cloud.mqtt_started = true;
    return ESP_OK;
}

void app_cloud_request_photo_upload(void)
{
    if (s_cloud.event_group != NULL)
    {
        xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_PHOTO_UPLOAD_BIT);
    }
}

static void app_cloud_delivery_photo_status_cb(void *user_ctx)
{
    (void)user_ctx;
    app_cloud_request_photo_upload();
}

// 云端后台任务：消费“启动 MQTT”事件，避免事件回调里做重操作。
static void app_cloud_task(void *arg)
{
    (void)arg;
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_cloud.event_group,
            APP_CLOUD_START_MQTT_BIT | APP_CLOUD_PHOTO_UPLOAD_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(5000));
        if ((bits & APP_CLOUD_START_MQTT_BIT) != 0)
        {
            esp_err_t ret = app_cloud_start_mqtt_if_needed();
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "defer mqtt start retry: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(2000));
                xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_START_MQTT_BIT);
            }
        }
        if (bits == 0 &&
            (xEventGroupGetBits(s_cloud.event_group) & APP_CLOUD_WIFI_CONNECTED_BIT) != 0 &&
            !s_cloud.mqtt_connected)
        {
            xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_START_MQTT_BIT);
        }
        if ((bits & APP_CLOUD_PHOTO_UPLOAD_BIT) != 0 || bits == 0)
        {
            if (s_cloud.mqtt_connected)
            {
                app_cloud_publish_current_state();
            }
            app_cloud_publish_pending_photo();
        }
    }
}
// Wi-Fi/IP 事件处理：断线清理 MQTT，拿到 IP 后启动时间、天气和 MQTT。
/* ---------- 系统事件回调 ---------- */

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
        app_cloud_ensure_dns_after_ip();
        app_cloud_start_sntp_once();
        app_cloud_start_weather_task_once();
        xEventGroupSetBits(s_cloud.event_group,
            APP_CLOUD_WIFI_CONNECTED_BIT | APP_CLOUD_START_MQTT_BIT);
    }
}
// MQTT 事件处理：连接后订阅命令主题，收到数据后交给消息子模块。
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
        s_cloud.mqtt_connected = true;
        xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
        (void)esp_mqtt_client_subscribe(s_cloud.mqtt_client,
            s_cloud.topic_cmd,
            CONFIG_SKY_MQTT_QOS);
        if (s_cloud.have_last_snapshot)
        {
            app_cloud_publish_task_snapshot_internal(&s_cloud.last_snapshot);
        }
        app_cloud_request_photo_upload();
        app_cloud_start_weather_task_once();
        app_cloud_notify_weather_task();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "EMQX mqtt disconnected");
        s_cloud.mqtt_connected = false;
        xEventGroupClearBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
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
// 云端模块总初始化入口，由 main 以后台任务方式启动。
/* ---------- 公共初始化与状态查询 ---------- */

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
    // 云端需要监听任务状态变化，及时发布 state。
    ESP_RETURN_ON_ERROR(app_task_register_event_callback(app_cloud_on_task_event, NULL),
        TAG,
        "register task callback failed");
    (void)app_delivery_photo_register_status_callback(app_cloud_delivery_photo_status_cb, NULL);
    (void)app_task_peek_snapshot(&s_cloud.last_snapshot);
    s_cloud.have_last_snapshot = true;
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
    s_cloud.inited = true;
    app_cloud_start_weather_task_once();
    return ESP_OK;
}
// 汇总给主屏状态灯使用的连接/天气状态。
void app_cloud_get_status(app_cloud_status_t *out)
{
    if (out == NULL)
    {
        return;
    }
    out->wifi_connected = app_cloud_is_wifi_connected();
    out->mqtt_connected = app_cloud_is_mqtt_connected();
    out->weather_simulated = s_cloud.weather_simulated;
    out->weather_docking_blocked = s_cloud.weather_docking_blocked;
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
