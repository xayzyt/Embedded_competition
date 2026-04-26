#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "app_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_DOCK_STATE_SEARCHING = 0,
    APP_DOCK_STATE_WRONG_ID,
    APP_DOCK_STATE_TRACKING,
    APP_DOCK_STATE_ALIGNED,
    APP_DOCK_STATE_READY_TO_DOCK,
} app_dock_state_t;

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

    /* 100 mm AprilTag 的粗距离估算参数（后续可标定） */
    bool use_distance_gate;
    int32_t tag_size_mm;
    float focal_length_px;
    int32_t min_distance_mm;
    int32_t max_distance_mm;

    /* 平滑与防抖参数 */
    uint8_t ema_shift;                /* 2 表示 1/4 新值权重 */
    uint8_t ready_enter_frames;       /* 连续满足 ready 条件多少帧才进入 ready */
    uint8_t ready_exit_bad_frames;    /* 连续多少帧不满足才退出 ready */
    uint8_t aligned_enter_frames;     /* 连续多少帧满足对准条件才进入 aligned */
    uint8_t wrong_id_enter_frames;    /* 连续多少帧错误 ID 才显示 wrong_id */
    uint8_t lost_hold_frames;         /* 丢 1~N 帧时保留上一个稳定状态 */
} app_dock_judge_config_t;

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
