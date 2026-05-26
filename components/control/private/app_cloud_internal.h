#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "app_task.h"

#ifdef __cplusplus
extern "C" {
#endif

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
#define APP_CLOUD_WEATHER_TEXT_LEN     96
#define APP_CLOUD_WEATHER_URL_LEN      256
#define APP_CLOUD_WEATHER_RESP_LEN     1024
#define APP_CLOUD_WEATHER_TASK_STACK   (6 * 1024)
#define APP_CLOUD_WEATHER_TASK_PRIO    4
#define APP_CLOUD_WEATHER_RETRY_MS     (30 * 1000)
#define APP_CLOUD_WEATHER_REFRESH_MS   (30 * 60 * 1000)
#define APP_CLOUD_WEATHER_FALLBACK_CODE 4
#define APP_CLOUD_WEATHER_SEVERE_CODE  36
#define APP_CLOUD_ABORT_WAIT_MS        800
#define APP_CLOUD_WEATHER_TOGGLE_DEBOUNCE_MS (800)
#define APP_CLOUD_WEATHER_RESTORE_REFETCH_MIN_MS (30 * 1000)

typedef enum {
    APP_CLOUD_WEATHER_MODE_NORMAL = 0,
    APP_CLOUD_WEATHER_MODE_CLOUD_GUARD,
    APP_CLOUD_WEATHER_MODE_EMERGENCY,
} app_cloud_weather_mode_t;

typedef struct {
    bool inited;
    bool mqtt_started;
    bool mqtt_connected;
    bool sntp_started;
    bool weather_simulated;
    bool weather_docking_blocked;
    bool weather_docking_policy_applied;
    bool weather_restore_pending;
    bool have_cached_weather;
    bool have_last_snapshot;
    uint32_t msg_seq;
    TickType_t weather_cache_tick;
    TickType_t weather_last_toggle_tick;
    EventGroupHandle_t event_group;
    esp_netif_t *sta_netif;
    esp_mqtt_client_handle_t mqtt_client;
    app_task_snapshot_t last_snapshot;
    char current_request_id[32];
    char current_order_id[48];
    char current_order_name[32];
    char topic_cmd[APP_CLOUD_TOPIC_BUF_LEN];
    char topic_ack[APP_CLOUD_TOPIC_BUF_LEN];
    char topic_state[APP_CLOUD_TOPIC_BUF_LEN];
    char cached_weather_text[APP_CLOUD_WEATHER_TEXT_LEN];
    int cached_weather_code;
    app_cloud_weather_mode_t weather_mode;
} app_cloud_runtime_t;

extern app_cloud_runtime_t s_cloud;

void app_cloud_start_weather_task_once(void);
void app_cloud_notify_weather_task(void);
void app_cloud_publish_current_state(void);
void app_cloud_publish_task_snapshot_internal(const app_task_snapshot_t *snap);
void app_cloud_on_task_event(app_task_event_t event,
    const app_task_snapshot_t *snap,
    void *user_ctx);
void app_cloud_handle_mqtt_data_event(esp_mqtt_event_handle_t event);

#ifdef __cplusplus
}
#endif
