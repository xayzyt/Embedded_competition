#ifndef APP_CLOUD_H
#define APP_CLOUD_H
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t app_cloud_init(void);
bool app_cloud_is_wifi_connected(void);
bool app_cloud_is_mqtt_connected(void);
void app_cloud_set_weather_simulated(bool simulated);
void app_cloud_trigger_weather_emergency(void);
bool app_cloud_is_weather_simulated(void);
bool app_cloud_is_weather_docking_blocked(void);
#ifdef __cplusplus
}
#endif
#endif
