#include "app_camera_route.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_ai_capture.h"
#include "app_drone_ai.h"
#include "app_task.h"
#include "app_vision.h"

// 相机帧路由：按任务阶段把同一路视频流分配给 AI、AprilTag 和样本抓图。

#define VISION_SAMPLE_INTERVAL   2
#define DRONE_AI_SAMPLE_INTERVAL 8
#define CAMERA_DIAG_INTERVAL_MS  2000U

static const char *TAG = "app_camera";

typedef struct {
    uint32_t vision_sample_skip;
    uint32_t drone_ai_sample_skip;
    bool apriltag_gate_was_open;
    uint32_t ai_submit_count;
    uint32_t vision_submit_count;
    uint32_t capture_submit_count;
    uint32_t diag_last_ms;
    uint32_t diag_last_frame_count;
    uint32_t diag_last_display_count;
    uint32_t diag_last_stage_drop_count;
    uint32_t diag_last_ai_submit_count;
    uint32_t diag_last_vision_submit_count;
    uint32_t diag_last_capture_submit_count;
    uint32_t diag_last_ai_infer_count;
    uint32_t diag_last_ai_drop_count;
    uint32_t diag_last_vision_drop_count;
    uint32_t diag_last_bad_len_count;
    uint32_t diag_last_bad_preview_count;
    uint32_t diag_last_ppa_guard_count;
    uint32_t diag_last_cpu_fallback_count;
    uint32_t diag_last_raw_bad_count;
    uint32_t diag_last_canvas_bad_count;
} app_camera_route_runtime_t;

static app_camera_route_runtime_t s_route = {0};

static bool app_camera_route_apriltag_gate_open(void)
{
    app_task_snapshot_t task = {0};
    if (!app_task_peek_snapshot(&task))
    {
        return false;
    }
    // AprilTag 只有在 AI 已确认无人机后才放行，降低视觉检测负载。
    return task.active &&
           (task.state == APP_TASK_STATE_WAIT_APPROACH ||
            task.state == APP_TASK_STATE_AUTH_PASSED) &&
           app_drone_ai_is_drone_confirmed();
}

static bool app_camera_route_ai_gate_active(void)
{
    app_task_snapshot_t task = {0};
    if (!app_task_peek_snapshot(&task))
    {
        return false;
    }
    // 任务等待靠近且 AI 尚未确认时，优先抽帧给无人机识别。
    return task.active &&
           (task.state == APP_TASK_STATE_WAIT_APPROACH) &&
           !app_drone_ai_is_drone_confirmed();
}

void app_camera_route_reset(void)
{
    memset(&s_route, 0, sizeof(s_route));
}

app_camera_frame_route_t app_camera_route_select(void)
{
    app_camera_frame_route_t route = {0};
    const bool capture_active = app_ai_capture_is_active();
    const bool ai_gate_active = app_camera_route_ai_gate_active();
    const bool apriltag_gate_open = app_camera_route_apriltag_gate_open();
    // AI 与视觉都采用降采样节拍，避免每帧都进入重计算模块。
    if (!ai_gate_active)
    {
        s_route.drone_ai_sample_skip = 0;
    }
    else if (++s_route.drone_ai_sample_skip >= DRONE_AI_SAMPLE_INTERVAL)
    {
        s_route.drone_ai_sample_skip = 0;
        route.ai_due = true;
    }
    if (capture_active || !apriltag_gate_open)
    {
        s_route.vision_sample_skip = 0;
        s_route.apriltag_gate_was_open = false;
    }
    else if (!s_route.apriltag_gate_was_open)
    {
        s_route.vision_sample_skip = 0;
        s_route.apriltag_gate_was_open = true;
        route.vision_due = true;
    }
    else if (++s_route.vision_sample_skip >= VISION_SAMPLE_INTERVAL)
    {
        s_route.vision_sample_skip = 0;
        route.vision_due = true;
    }
    // 抓图由独立开关控制，方便现场采集训练样本。
    route.capture_due = app_ai_capture_should_capture_frame();
    return route;
}

void app_camera_route_note_ai_submit(void)
{
    s_route.ai_submit_count++;
}

void app_camera_route_note_vision_submit(void)
{
    s_route.vision_submit_count++;
}

void app_camera_route_note_capture_submit(void)
{
    s_route.capture_submit_count++;
}

void app_camera_route_maybe_log_diag(uint32_t frame_count,
    uint32_t display_count,
    uint32_t stage_drop_count,
    uint32_t bad_len_count,
    uint32_t bad_preview_count,
    uint32_t ppa_guard_count,
    uint32_t cpu_fallback_count,
    uint32_t raw_bad_count,
    uint32_t canvas_bad_count)
{
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    app_drone_ai_stats_t ai = {0};
    app_vision_stats_t vision = {0};
    app_drone_ai_get_stats(&ai);
    app_vision_get_stats(&vision);
    const uint32_t vision_drop = vision.busy_drop + vision.overwrite;
    if (s_route.diag_last_ms == 0U)
    {
        s_route.diag_last_ms = now_ms;
        s_route.diag_last_frame_count = frame_count;
        s_route.diag_last_display_count = display_count;
        s_route.diag_last_stage_drop_count = stage_drop_count;
        s_route.diag_last_ai_submit_count = s_route.ai_submit_count;
        s_route.diag_last_vision_submit_count = s_route.vision_submit_count;
        s_route.diag_last_capture_submit_count = s_route.capture_submit_count;
        s_route.diag_last_ai_infer_count = ai.inferred;
        s_route.diag_last_ai_drop_count = ai.dropped;
        s_route.diag_last_vision_drop_count = vision_drop;
        s_route.diag_last_bad_len_count = bad_len_count;
        s_route.diag_last_bad_preview_count = bad_preview_count;
        s_route.diag_last_ppa_guard_count = ppa_guard_count;
        s_route.diag_last_cpu_fallback_count = cpu_fallback_count;
        s_route.diag_last_raw_bad_count = raw_bad_count;
        s_route.diag_last_canvas_bad_count = canvas_bad_count;
        return;
    }
    if ((now_ms - s_route.diag_last_ms) < CAMERA_DIAG_INTERVAL_MS)
    {
        return;
    }
    ESP_LOGI(TAG,
        "diag 2s: capture=%lu display=%lu stage_drop=%lu ai_submit=%lu ai_infer=%lu ai_drop=%lu vision_submit=%lu vision_drop=%lu save_submit=%lu confirmed=%d bad_len=%lu bad_preview=%lu ppa_guard=%lu cpu_fallback=%lu raw_bad=%lu canvas_bad=%lu",
        (unsigned long)(frame_count - s_route.diag_last_frame_count),
        (unsigned long)(display_count - s_route.diag_last_display_count),
        (unsigned long)(stage_drop_count - s_route.diag_last_stage_drop_count),
        (unsigned long)(s_route.ai_submit_count - s_route.diag_last_ai_submit_count),
        (unsigned long)(ai.inferred - s_route.diag_last_ai_infer_count),
        (unsigned long)(ai.dropped - s_route.diag_last_ai_drop_count),
        (unsigned long)(s_route.vision_submit_count - s_route.diag_last_vision_submit_count),
        (unsigned long)(vision_drop - s_route.diag_last_vision_drop_count),
        (unsigned long)(s_route.capture_submit_count - s_route.diag_last_capture_submit_count),
        (int)ai.confirmed,
        (unsigned long)(bad_len_count - s_route.diag_last_bad_len_count),
        (unsigned long)(bad_preview_count - s_route.diag_last_bad_preview_count),
        (unsigned long)(ppa_guard_count - s_route.diag_last_ppa_guard_count),
        (unsigned long)(cpu_fallback_count - s_route.diag_last_cpu_fallback_count),
        (unsigned long)(raw_bad_count - s_route.diag_last_raw_bad_count),
        (unsigned long)(canvas_bad_count - s_route.diag_last_canvas_bad_count));
    s_route.diag_last_ms = now_ms;
    s_route.diag_last_frame_count = frame_count;
    s_route.diag_last_display_count = display_count;
    s_route.diag_last_stage_drop_count = stage_drop_count;
    s_route.diag_last_ai_submit_count = s_route.ai_submit_count;
    s_route.diag_last_vision_submit_count = s_route.vision_submit_count;
    s_route.diag_last_capture_submit_count = s_route.capture_submit_count;
    s_route.diag_last_ai_infer_count = ai.inferred;
    s_route.diag_last_ai_drop_count = ai.dropped;
    s_route.diag_last_vision_drop_count = vision_drop;
    s_route.diag_last_bad_len_count = bad_len_count;
    s_route.diag_last_bad_preview_count = bad_preview_count;
    s_route.diag_last_ppa_guard_count = ppa_guard_count;
    s_route.diag_last_cpu_fallback_count = cpu_fallback_count;
    s_route.diag_last_raw_bad_count = raw_bad_count;
    s_route.diag_last_canvas_bad_count = canvas_bad_count;
}
