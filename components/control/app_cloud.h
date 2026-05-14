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

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOUD_H */
