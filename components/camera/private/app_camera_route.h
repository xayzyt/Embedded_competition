#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 单帧路由决策：同一帧可被分发给 AI、AprilTag 或抓图保存。
typedef struct {
    bool ai_due;
    bool vision_due;
    bool capture_due;
} app_camera_frame_route_t;

// 重置采样节拍和诊断计数。
void app_camera_route_reset(void);
// 根据任务状态、AI 门控和采样间隔决定当前帧去向。
app_camera_frame_route_t app_camera_route_select(void);
// 记录实际提交数，用于周期诊断日志。
void app_camera_route_note_ai_submit(void);
void app_camera_route_note_vision_submit(void);
void app_camera_route_note_capture_submit(void);
// 每隔固定时间输出相机、AI、视觉和抓图吞吐情况。
void app_camera_route_maybe_log_diag(uint32_t frame_count,
    uint32_t display_count,
    uint32_t stage_drop_count,
    uint32_t bad_len_count,
    uint32_t bad_preview_count,
    uint32_t ppa_guard_count,
    uint32_t cpu_fallback_count,
    uint32_t raw_bad_count,
    uint32_t canvas_bad_count);

#ifdef __cplusplus
}
#endif
