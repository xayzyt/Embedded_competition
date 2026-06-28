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

// 云端模块内部共享状态：app_cloud.c、消息处理和天气任务共用。

#ifdef __cplusplus
extern "C" {
#endif

// 云端模块内部事件位和固定缓冲区长度。
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
#define APP_CLOUD_MANUAL_RETRACT_WAIT_MS 3000U

// 天气展示/保护模式。
typedef enum {
    APP_CLOUD_WEATHER_MODE_NORMAL = 0,
    APP_CLOUD_WEATHER_MODE_CLOUD_GUARD,
    APP_CLOUD_WEATHER_MODE_EMERGENCY,
} app_cloud_weather_mode_t;

// 云端运行期上下文，由 app_cloud.c 初始化，消息和天气子模块共享。
typedef struct {
    bool inited;                         // 云端模块是否完成初始化。
    bool mqtt_started;                   // MQTT 客户端是否已启动。
    bool mqtt_connected;                 // MQTT 是否在线。
    bool sntp_started;                   // SNTP 是否已启动。
    bool weather_simulated;              // 是否处于天气模拟模式。
    bool weather_docking_blocked;        // 天气策略是否阻止对接/取货。
    bool weather_docking_policy_applied; // 天气策略是否已经下发到任务/UI。
    bool weather_restore_pending;        // 解除模拟后是否等待刷新真实天气。
    bool have_cached_weather;            // 是否有可复用天气缓存。
    bool have_last_snapshot;             // 是否缓存了最近任务快照。
    uint32_t msg_seq;                    // 状态上报序号。
    TickType_t weather_cache_tick;       // 天气缓存更新时间。
    TickType_t weather_last_toggle_tick; // 天气模拟按钮防抖时间。
    EventGroupHandle_t event_group;      // Wi-Fi/MQTT/天气任务同步事件组。
    esp_netif_t *sta_netif;              // STA 网络接口。
    esp_mqtt_client_handle_t mqtt_client; // MQTT 客户端句柄。
    app_task_snapshot_t last_snapshot;   // 最近一次任务快照。
    char current_request_id[32];         // 当前云端请求 ID。
    char current_order_id[48];           // 当前订单 ID。
    char current_order_name[32];         // 当前订单名。
    char topic_cmd[APP_CLOUD_TOPIC_BUF_LEN];   // 命令订阅主题。
    char topic_ack[APP_CLOUD_TOPIC_BUF_LEN];   // ACK 发布主题。
    char topic_state[APP_CLOUD_TOPIC_BUF_LEN]; // 状态发布主题。
    char cached_weather_text[APP_CLOUD_WEATHER_TEXT_LEN]; // 最近天气文案。
    int cached_weather_code;             // 最近天气代码。
    app_cloud_weather_mode_t weather_mode; // 当前天气模式。
} app_cloud_runtime_t;

extern app_cloud_runtime_t s_cloud;

// 内部跨文件接口：天气任务、状态发布和 MQTT 数据分发。
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
