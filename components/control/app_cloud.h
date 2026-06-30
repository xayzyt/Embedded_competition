#ifndef APP_CLOUD_H
#define APP_CLOUD_H
#include <stdbool.h>
#include "esp_err.h"
// 云端公共接口：负责 Wi-Fi/MQTT/天气状态，并把任务状态同步到外部平台。

#ifdef __cplusplus
extern "C" {
#endif
// 初始化 Wi-Fi、MQTT、时间同步和云端任务。
esp_err_t app_cloud_init(void);
// 云端连接与天气策略状态快照。
typedef struct {
    bool wifi_connected;          // Wi-Fi 是否在线。
    bool mqtt_connected;          // MQTT 是否在线。
    bool weather_simulated;       // 是否使用模拟恶劣天气。
    bool weather_docking_blocked; // 天气策略是否禁止对接/取货。
} app_cloud_status_t;
// 读取云端模块状态。
void app_cloud_get_status(app_cloud_status_t *out);
bool app_cloud_is_wifi_connected(void);
bool app_cloud_is_mqtt_connected(void);
// 设置或触发天气保护。
void app_cloud_set_weather_simulated(bool simulated);
void app_cloud_trigger_weather_emergency(void);
esp_err_t app_cloud_trigger_weather_emergency_wait(void);
esp_err_t app_cloud_trigger_weather_demo_protection_wait(void);
// 查询天气保护状态。
bool app_cloud_is_weather_simulated(void);
bool app_cloud_is_weather_docking_blocked(void);
#ifdef __cplusplus
}
#endif
#endif
