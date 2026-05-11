#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_DRONE_AI_CLASS_NODRONE = 0,
    APP_DRONE_AI_CLASS_DRONE = 1,
} app_drone_ai_class_t;

typedef struct {
    bool valid;
    bool confirmed;
    app_drone_ai_class_t label;
    float nodrone_score;
    float drone_score;
    uint8_t hit_count;
    uint32_t frame_seq;
    uint32_t infer_ms;
} app_drone_ai_result_t;

esp_err_t app_drone_ai_init(void);

esp_err_t app_drone_ai_submit_frame(const uint8_t *rgb565,
                                    uint32_t width,
                                    uint32_t height,
                                    size_t len);

bool app_drone_ai_get_latest_result(app_drone_ai_result_t *out);

bool app_drone_ai_is_model_ready(void);

esp_err_t app_drone_ai_wait_ready(uint32_t timeout_ms);

bool app_drone_ai_is_drone_confirmed(void);

void app_drone_ai_reset_gate(void);

void app_drone_ai_format_status(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
