#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_dock_types.h"
#include "app_vision.h"
// 对接判定接口：把 AprilTag 视觉结果转成可执行的门控状态。

#ifdef __cplusplus
extern "C" {
#endif
// 对接判定参数，控制目标 ID、中心容差、距离门控和状态滞回。
typedef struct {
    bool use_target_id;           // 是否强制匹配目标标签 ID。
    uint16_t target_tag_id;       // 目标 AprilTag ID。
    int32_t center_x_ref;         // 屏幕/图像参考中心 X。
    int32_t center_y_ref;         // 屏幕/图像参考中心 Y。
    int32_t center_x_tol;         // X 方向允许偏差。
    int32_t center_y_tol;         // Y 方向允许偏差。
    int32_t min_area;             // 最小有效面积。
    int32_t min_bbox_w;           // 最小包围盒宽。
    int32_t min_bbox_h;           // 最小包围盒高。
    uint16_t min_stable_count;    // 判定稳定所需连续帧数。
    bool use_distance_gate;       // 是否启用距离上下限。
    int32_t tag_size_mm;          // 标签实际边长，单位 mm。
    float focal_length_px;        // 标定焦距，单位像素。
    int32_t min_distance_mm;      // 最小允许距离。
    int32_t max_distance_mm;      // 最大允许距离。
    uint8_t ema_shift;            // EMA 滤波强度，越大越平滑。
    uint8_t ready_enter_frames;   // 进入 ready 所需连续好帧。
    uint8_t ready_exit_bad_frames; // 退出 ready 所需连续坏帧。
    uint8_t aligned_enter_frames; // 进入 aligned 所需连续好帧。
    uint8_t wrong_id_enter_frames; // 连续错误 ID 判定帧数。
    uint8_t lost_hold_frames;     // 短时丢帧保持旧结果帧数。
} app_dock_judge_config_t;
// 获取默认参数并初始化/重置判定器。
void app_dock_judge_get_default_config(app_dock_judge_config_t *out);
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg);
esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter);
void app_dock_judge_reset(void);
// 输入视觉结果，输出带滞回和滤波后的对接判定。
bool app_dock_judge_process(const app_vision_result_t *vision,
                            app_dock_judge_result_t *out);
// 状态与结果格式化，供 UI/日志使用。
const char *app_dock_judge_state_to_text(app_dock_state_t state);
void app_dock_judge_format_status(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);
#ifdef __cplusplus
}
#endif
