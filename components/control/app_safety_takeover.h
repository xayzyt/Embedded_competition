#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "app_ch32_link.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_safety_takeover_start(void);
esp_err_t app_safety_takeover_trigger_typhoon(void);
void app_safety_takeover_mark_failed(void);
void app_safety_takeover_on_ch32_line(const app_ch32_line_t *msg);
bool app_safety_takeover_ai_monitor_enabled(void);
bool app_safety_takeover_preview_active(void);

#ifdef __cplusplus
}
#endif
