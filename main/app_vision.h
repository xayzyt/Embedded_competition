#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    uint16_t tag_id;
    uint8_t hamming;
    uint8_t rotation;
    uint8_t threshold;
    uint8_t border_dark_pct;
    int32_t center_x;
    int32_t center_y;
    int32_t area;
    int32_t bbox_x;
    int32_t bbox_y;
    int32_t bbox_w;
    int32_t bbox_h;

    int32_t corner_tl_x;
    int32_t corner_tl_y;
    int32_t corner_tr_x;
    int32_t corner_tr_y;
    int32_t corner_br_x;
    int32_t corner_br_y;
    int32_t corner_bl_x;
    int32_t corner_bl_y;

    float edge_px_avg;
    float top_edge_angle_deg;

    uint32_t src_width;
    uint32_t src_height;
    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    uint32_t gray_width;
    uint32_t gray_height;

    uint32_t frame_seq;
    uint32_t detect_ms;
    uint16_t stable_count;
    uint16_t lost_count;
} app_vision_result_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    size_t len;
    uint32_t seq;
    uint32_t tick_ms;
} app_vision_frame_info_t;

typedef struct {
    uint32_t src_width;
    uint32_t src_height;
    uint32_t gray_width;
    uint32_t gray_height;
    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    size_t gray_len;
    uint32_t seq;
    uint32_t tick_ms;
} app_vision_gray_frame_info_t;

esp_err_t app_vision_init(void);
esp_err_t app_vision_start(void);
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
                                  uint32_t width,
                                  uint32_t height,
                                  size_t len);
bool app_vision_get_latest_result(app_vision_result_t *out);

#ifdef __cplusplus
}
#endif
