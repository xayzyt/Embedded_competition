#include "app_camera_route.h"
#include <string.h>
#include "app_ai_capture.h"
#include "app_drone_ai.h"
#include "app_task.h"

#define VISION_SAMPLE_INTERVAL   2
#define DRONE_AI_SAMPLE_INTERVAL 8

typedef struct {
    uint32_t vision_sample_skip;
    uint32_t drone_ai_sample_skip;
    bool apriltag_gate_was_open;
} app_camera_route_runtime_t;

static app_camera_route_runtime_t s_route = {0};

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

    route.capture_due = app_ai_capture_should_capture_frame();
    return route;
}
