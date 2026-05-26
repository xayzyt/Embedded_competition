#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    APP_DOCK_STATE_SEARCHING = 0,
    APP_DOCK_STATE_WRONG_ID,
    APP_DOCK_STATE_TRACKING,
    APP_DOCK_STATE_ALIGNED,
    APP_DOCK_STATE_READY_TO_DOCK,
} app_dock_state_t;
typedef struct {
    app_dock_state_t state;
    bool vision_valid;
    bool target_id_ok;
    bool centered_ok;
    bool near_ok;
    bool stable_ok;
    bool distance_ok;
    uint16_t tag_id;
    int32_t dx;
    int32_t dy;
    int32_t area;
    int32_t bbox_w;
    int32_t bbox_h;
    uint16_t stable_count;
    uint16_t lost_count;
    uint32_t frame_seq;
    int32_t raw_dx;
    int32_t raw_dy;
    int32_t raw_area;
    int32_t filtered_center_x;
    int32_t filtered_center_y;
    int32_t filtered_area;
    float raw_edge_px;
    float filtered_edge_px;
    float angle_deg;
    int32_t est_distance_mm;
    uint8_t hover_score;
    uint8_t ready_pass_count;
    uint8_t ready_bad_count;
    uint8_t invalid_hold_count;
} app_dock_judge_result_t;
