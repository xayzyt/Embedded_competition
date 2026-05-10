#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "app_dock_types.h"
#include "app_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool use_target_id;
    uint16_t target_tag_id;

    int32_t center_x_ref;
    int32_t center_y_ref;
    int32_t center_x_tol;
    int32_t center_y_tol;

    int32_t min_area;
    int32_t min_bbox_w;
    int32_t min_bbox_h;
    uint16_t min_stable_count;

    bool use_distance_gate;
    int32_t tag_size_mm;
    float focal_length_px;
    int32_t min_distance_mm;
    int32_t max_distance_mm;

    uint8_t ema_shift;
    uint8_t ready_enter_frames;
    uint8_t ready_exit_bad_frames;
    uint8_t aligned_enter_frames;
    uint8_t wrong_id_enter_frames;
    uint8_t lost_hold_frames;
} app_dock_judge_config_t;

void app_dock_judge_get_default_config(app_dock_judge_config_t *out);

esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg);

esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter);

void app_dock_judge_reset(void);

bool app_dock_judge_process(const app_vision_result_t *vision,
                            app_dock_judge_result_t *out);

const char *app_dock_judge_state_to_text(app_dock_state_t state);

void app_dock_judge_format_status(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);

void app_dock_judge_format_detail(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);

#ifdef __cplusplus
}
#endif
