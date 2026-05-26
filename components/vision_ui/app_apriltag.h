#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool valid;
    uint16_t id;
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
} app_apriltag_result_t;
esp_err_t app_apriltag_init(void);
bool app_apriltag_detect_tag36h11(const uint8_t *gray,
                                  uint32_t width,
                                  uint32_t height,
                                  app_apriltag_result_t *out);
#ifdef __cplusplus
}
#endif
