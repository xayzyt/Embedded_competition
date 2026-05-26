#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t app_camera_init(void);
esp_err_t app_camera_preview_start(void);
bool app_camera_wait_first_frame(uint32_t timeout_ms);
void app_camera_pause(void);
void app_camera_resume(void);
#ifdef __cplusplus
}
#endif
