#pragma once

/*
 * Drone AI gate public interface.
 * The detailed per-frame inference result is internal to app_drone_ai.cpp.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t submitted;
    uint32_t inferred;
    uint32_t dropped;
    bool confirmed;
} app_drone_ai_stats_t;

esp_err_t app_drone_ai_init(void);

esp_err_t app_drone_ai_submit_frame(const uint8_t *rgb565,
                                    uint32_t width,
                                    uint32_t height,
                                    size_t len);

esp_err_t app_drone_ai_wait_ready(uint32_t timeout_ms);

bool app_drone_ai_is_drone_confirmed(void);

void app_drone_ai_reset_gate(void);

void app_drone_ai_format_status(char *buf, size_t buf_len);

void app_drone_ai_get_stats(app_drone_ai_stats_t *out);

#ifdef __cplusplus
}
#endif
