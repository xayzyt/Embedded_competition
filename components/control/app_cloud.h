#ifndef APP_CLOUD_H
#define APP_CLOUD_H

/*
 * Wi-Fi/MQTT 云端命令桥接模块。
 * 在 ESP32-P4 上通过板载 C6 的 ESP-Hosted/esp_wifi_remote 接入网络。
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 Wi-Fi STA、MQTT topic 和任务状态发布。 */
esp_err_t app_cloud_init(void);

/* 查询 Wi-Fi STA 是否已连接。 */
bool app_cloud_is_wifi_connected(void);

/* 查询 MQTT 是否已连接到 broker。 */
bool app_cloud_is_mqtt_connected(void);

/* 模拟极端恶劣天气：天气固定为台风 28℃，并禁止/停止接驳。 */
void app_cloud_simulate_severe_weather(void);

/* 查询当前天气是否禁止接驳。 */
bool app_cloud_is_weather_docking_blocked(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOUD_H */
