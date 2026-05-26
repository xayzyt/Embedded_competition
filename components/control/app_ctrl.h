#pragma once
#include "esp_err.h"
#include "app_ch32_link.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t app_ctrl_init(void);
esp_err_t app_ctrl_start(void);
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx);
#ifdef __cplusplus
}
#endif
