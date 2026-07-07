#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化仅用于扬声器的 TX-only I2S/ES8311 后端。
esp_err_t app_audio_prompt_hw_init(void);

// 释放本模块独占的 codec/I2S 资源；共享 BSP I2C 总线保持不变。
void app_audio_prompt_hw_deinit(void);

bool app_audio_prompt_hw_is_ready(void);
bool app_audio_prompt_hw_dma_budget_ok(const char *stage);
esp_err_t app_audio_prompt_hw_write(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
