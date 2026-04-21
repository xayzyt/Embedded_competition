#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_camera_init(void);
esp_err_t app_camera_preview_start(void);
esp_err_t app_camera_preview_stop(void);
bool app_camera_is_preview_running(void);

#ifdef __cplusplus
}
#endif
