#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_ai_capture_init(void);
esp_err_t app_ai_capture_start(void);
void app_ai_capture_stop(void);

bool app_ai_capture_is_active(void);
bool app_ai_capture_should_capture_frame(void);

esp_err_t app_ai_capture_submit_frame(const uint8_t *rgb565,
                                      uint32_t width,
                                      uint32_t height,
                                      size_t len);

#ifdef __cplusplus
}
#endif
