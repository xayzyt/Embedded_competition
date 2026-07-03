#include "app_camera_route.h"
#include <string.h>
#include "app_ai_capture.h"
#include "app_delivery_photo.h"
#include "app_drone_ai.h"
#include "app_task.h"

// 视觉检测频率高于无人机分类频率，避免两个重任务每帧同时运行。
#define VISION_SAMPLE_INTERVAL   6
#define DRONE_AI_SAMPLE_INTERVAL 8
#define APRILTAG_GATE_SETTLE_FRAMES 12U

// 这些计数器只由相机帧回调访问，不需要额外加锁。
typedef struct {
    uint32_t vision_sample_skip;   // 距离上次视觉提交已经跳过的帧数。
    uint32_t drone_ai_sample_skip; // 距离上次 AI 提交已经跳过的帧数。
    uint32_t apriltag_settle_remaining; // AI 确认后先保留几帧预览，避免立刻压垮显示链路。
    bool apriltag_gate_was_open;   // 用于识别门控从关闭到打开的边沿。
} app_camera_route_runtime_t;

static app_camera_route_runtime_t s_route = {0};

// AprilTag 只在任务等待靠近/已鉴权阶段运行，并要求先确认画面中存在无人机。
static bool app_camera_route_apriltag_gate_open(void)
{
    app_task_snapshot_t task = {0};
    if (!app_task_peek_snapshot(&task))
    {
        return false;
    }
    return task.active &&
           (task.state == APP_TASK_STATE_WAIT_APPROACH ||
            task.state == APP_TASK_STATE_AUTH_PASSED) &&
           app_drone_ai_is_drone_confirmed();
}

// 无人机分类只负责打开视觉门控；确认后立即停止继续消耗推理算力。
static bool app_camera_route_ai_gate_active(void)
{
    app_task_snapshot_t task = {0};
    if (!app_task_peek_snapshot(&task))
    {
        return false;
    }
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

    // AI 门控关闭时重置计数，保证下次任务从完整抽样周期开始。
    if (!ai_gate_active)
    {
        s_route.drone_ai_sample_skip = 0;
    }
    else if (++s_route.drone_ai_sample_skip >= DRONE_AI_SAMPLE_INTERVAL)
    {
        s_route.drone_ai_sample_skip = 0;
        route.ai_due = true;
    }

    // 抓图时暂停视觉提交，避免文件采样与 AprilTag 检测争抢同一帧资源。
    if (capture_active || !apriltag_gate_open)
    {
        s_route.vision_sample_skip = 0;
        s_route.apriltag_settle_remaining = 0;
        s_route.apriltag_gate_was_open = false;
    }
    else if (!s_route.apriltag_gate_was_open)
    {
        s_route.vision_sample_skip = 0;
        s_route.apriltag_gate_was_open = true;
        s_route.apriltag_settle_remaining = APRILTAG_GATE_SETTLE_FRAMES;
    }
    else if (s_route.apriltag_settle_remaining > 0U)
    {
        s_route.apriltag_settle_remaining--;
    }
    else if (++s_route.vision_sample_skip >= VISION_SAMPLE_INTERVAL)
    {
        s_route.vision_sample_skip = 0;
        route.vision_due = true;
    }

    // 抓图模块内部维护采样间隔和队列容量，路由层直接采用其决策。
    route.capture_due = app_ai_capture_should_capture_frame();
    route.delivery_due = app_delivery_photo_should_capture_frame();
    return route;
}
