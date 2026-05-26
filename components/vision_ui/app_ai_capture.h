#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    APP_AI_CAPTURE_MODE_DRONE = 0,
    APP_AI_CAPTURE_MODE_NO_DRONE,
    APP_AI_CAPTURE_MODE_COUNT,
} app_ai_capture_mode_t;
esp_err_t app_ai_capture_init(void);
esp_err_t app_ai_capture_start(void);
void app_ai_capture_stop(void);
esp_err_t app_ai_capture_set_mode(app_ai_capture_mode_t mode);
app_ai_capture_mode_t app_ai_capture_get_mode(void);
app_ai_capture_mode_t app_ai_capture_toggle_mode(void);
const char *app_ai_capture_mode_label(app_ai_capture_mode_t mode);
bool app_ai_capture_is_active(void);
bool app_ai_capture_should_capture_frame(void);
esp_err_t app_ai_capture_submit_frame(const uint8_t *rgb565,
                                      uint32_t width,
                                      uint32_t height,
                                      size_t len);
#ifdef __cplusplus
}
#endif
