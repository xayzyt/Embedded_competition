#ifndef APP_CLOUD_H
#define APP_CLOUD_H
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t app_cloud_init(void);
typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    bool weather_simulated;
    bool weather_docking_blocked;
} app_cloud_status_t;
void app_cloud_get_status(app_cloud_status_t *out);
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
