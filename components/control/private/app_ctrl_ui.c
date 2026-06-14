#include "app_ctrl_ui.h"

#include <stdio.h>
#include <string.h>
#include "app_ch32_link.h"
#include "app_ctrl_proto.h"
#include "app_drone_ai.h"
#include "app_ui.h"

// 根据控制结果生成状态栏、调试信息和四阶段流程条。
// 中文常量使用 Unicode 转义，避免源码编码变化造成乱码。
#define UI_TEXT_WAIT          "\u7B49\u5F85"
#define UI_TEXT_WAIT_CLOUD    "\u7B49\u5F85\u4E91\u7AEF"
#define UI_TEXT_WAIT_TAG      "\u7B49\u5F85Tag"
#define UI_TEXT_WAIT_DOCK     "\u7B49\u5F85\u63A5\u9A73"
#define UI_TEXT_NOT_DONE      "\u672A\u5B8C\u6210"
#define UI_TEXT_DOCK          "\u63A5\u9A73"
#define UI_TEXT_DOCK_DONE     "\u63A5\u9A73\u5B8C\u6210"
#define UI_TEXT_DOCK_ERROR    "\u63A5\u9A73ERR"
#define UI_TEXT_DOCK_WAIT     "\u63A5\u9A73\u7B49\u5F85"
#define UI_TEXT_PICKUP        "\u53D6\u8D27"
#define UI_TEXT_DONE          "\u5B8C\u6210"

/* ---------- 状态文案 ---------- */

// 生成视觉、距离、稳定度、机械阶段和重量调试信息。
static void app_ctrl_compose_detail(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_dock_judge_result_t *dock = &cycle->dock;
    const app_ctrl_runtime_view_t *runtime = &cycle->runtime;
    if (!dock->vision_valid)
    {
        if (dock->state == APP_DOCK_STATE_SEARCHING)
        {
            strlcpy(buf, "dock dbg: wait valid tag", buf_len);
            return;
        }

        snprintf(buf,
            buf_len,
            "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ldmm e:%.1f stage:%s",
            (unsigned)dock->invalid_hold_count,
            (unsigned)dock->lost_count,
            (long)dock->dx,
            (long)dock->dy,
            (long)dock->est_distance_mm,
            (double)dock->filtered_edge_px,
            app_ch32_link_proto_stage_name(runtime->proto_stage));
        return;
    }

    snprintf(buf,
        buf_len,
        "dock dbg: id:%u c:%ld,%ld b:%ldx%ld dx:%ld dy:%ld z:%ldmm e:%.1f ang:%d st:%u score:%u wt:%s%ldg",
        (unsigned)dock->tag_id,
        (long)dock->filtered_center_x,
        (long)dock->filtered_center_y,
        (long)dock->bbox_w,
        (long)dock->bbox_h,
        (long)dock->dx,
        (long)dock->dy,
        (long)dock->est_distance_mm,
        (double)dock->filtered_edge_px,
        (int)dock->angle_deg,
        (unsigned)dock->stable_count,
        (unsigned)dock->hover_score,
        runtime->has_weight ? "" : "-",
        runtime->has_weight ? (long)runtime->weight_g : 0L);
}

// 根据第一个未满足的对接条件生成操作提示。
static void app_ctrl_compose_guidance(const app_dock_judge_result_t *dock,
    char *buf,
    size_t buf_len)
{
    if (!dock->vision_valid)
    {
        strlcpy(buf, "dock: searching target", buf_len);
    }
    else if (!dock->target_id_ok)
    {
        strlcpy(buf, "dock: wrong tag id", buf_len);
    }
    else if (!dock->centered_ok)
    {
        strlcpy(buf, "dock: align target center", buf_len);
    }
    else if (!dock->near_ok)
    {
        strlcpy(buf, "dock: move target closer", buf_len);
    }
    else if (!dock->stable_ok)
    {
        strlcpy(buf, "dock: hold hover stable", buf_len);
    }
    else if (!dock->distance_ok)
    {
        if (dock->est_distance_mm <= 0)
        {
            strlcpy(buf, "dock: wait valid distance", buf_len);
        }
        else
        {
            strlcpy(buf,
                dock->est_distance_mm < 260 ?
                    "dock: target too near" :
                    "dock: target too far",
                buf_len);
        }
    }
    else
    {
        app_dock_judge_format_status(dock, buf, buf_len);
    }
}

// 根据高层任务状态生成基础状态文案。
static void app_ctrl_compose_task_status(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_task_snapshot_t *task = &cycle->task;
    switch (task->state) {
    case APP_TASK_STATE_CONFIGURED:
        snprintf(buf,
            buf_len,
            cycle->runtime.ch32_ready ?
                "task: target=%u / remote ready" :
                "task: target=%u / wait CH32",
            (unsigned)task->target_id);
        break;

    case APP_TASK_STATE_WAIT_APPROACH: {
        char guide[72] = {0};
        if (cycle->apriltag_enabled)
        {
            app_ctrl_compose_guidance(&cycle->dock, guide, sizeof(guide));
        }
        else
        {
            app_drone_ai_format_status(guide, sizeof(guide));
        }
        snprintf(buf, buf_len, "task: wait id=%u / %s",
            (unsigned)task->target_id,
            guide);
        break;
    }

    case APP_TASK_STATE_AUTH_PASSED:
        snprintf(buf,
            buf_len,
            "task: auth passed / matched id=%u",
            (unsigned)(task->matched_tag_id != 0U ?
                task->matched_tag_id :
                task->target_id));
        break;

    case APP_TASK_STATE_DOCKING:
        strlcpy(buf, "task: docking in progress", buf_len);
        break;

    case APP_TASK_STATE_COMPLETED:
        snprintf(buf, buf_len, "task: completed / target=%u",
            (unsigned)task->target_id);
        break;

    case APP_TASK_STATE_FAULT:
        snprintf(buf, buf_len, "task: fault / %s",
            task->note[0] != '\0' ? task->note : "check CH32");
        break;

    case APP_TASK_STATE_CANCELLED:
        snprintf(buf, buf_len, "task: cancelled / target=%u",
            (unsigned)task->target_id);
        break;

    case APP_TASK_STATE_IDLE:
    default:
        strlcpy(buf, "task: idle", buf_len);
        break;
    }
}

static void app_ctrl_flow_set_detail(app_ui_flow_snapshot_t *flow,
    app_ui_flow_step_t step,
    const char *text)
{
    strlcpy(flow->step_detail[step], text, sizeof(flow->step_detail[0]));
}

/* ---------- 四阶段流程条 ---------- */

static void app_ctrl_flow_complete(app_ui_flow_snapshot_t *flow,
    app_ui_flow_step_t step,
    const char *detail)
{
    flow->step_state[step] = APP_UI_FLOW_STATE_DONE;
    app_ctrl_flow_set_detail(flow, step, detail);
}

static void app_ctrl_flow_activate(app_ui_flow_snapshot_t *flow,
    app_ui_flow_step_t step,
    const char *headline,
    const char *detail)
{
    flow->active_step = step;
    flow->step_state[step] = APP_UI_FLOW_STATE_ACTIVE;
    strlcpy(flow->headline, headline, sizeof(flow->headline));
    app_ctrl_flow_set_detail(flow, step, detail);
}

static void app_ctrl_flow_fail(app_ui_flow_snapshot_t *flow,
    app_ui_flow_step_t step,
    const char *headline,
    const char *detail)
{
    flow->active_step = step;
    flow->step_state[step] = APP_UI_FLOW_STATE_ERROR;
    strlcpy(flow->headline, headline, sizeof(flow->headline));
    app_ctrl_flow_set_detail(flow, step, detail);
}

static void app_ctrl_flow_init(app_ui_flow_snapshot_t *flow)
{
    static const char *const details[APP_UI_FLOW_STEP_COUNT] = {
        UI_TEXT_WAIT,
        UI_TEXT_WAIT_TAG,
        UI_TEXT_WAIT_DOCK,
        UI_TEXT_NOT_DONE,
    };

    // 每次从默认状态重新生成流程条。
    memset(flow, 0, sizeof(*flow));
    flow->active_step = APP_UI_FLOW_STEP_DRONE;
    strlcpy(flow->headline, UI_TEXT_WAIT_CLOUD, sizeof(flow->headline));
    for (int i = 0; i < APP_UI_FLOW_STEP_COUNT; i++)
    {
        flow->step_state[i] = APP_UI_FLOW_STATE_WAITING;
        app_ctrl_flow_set_detail(flow, (app_ui_flow_step_t)i, details[i]);
    }
}

// 将 CH32 机械阶段转换为流程条使用的短文案。
static const char *app_ctrl_proto_stage_action_text(app_ch32_proto_stage_t stage)
{
    switch (stage) {
    case APP_CH32_STAGE_DOOR_OPENING:    return "\u5F00";
    case APP_CH32_STAGE_DOOR_OPENED:     return "\u5DF2\u5F00";
    case APP_CH32_STAGE_TRAY_EXTENDING:  return UI_TEXT_DOCK;
    case APP_CH32_STAGE_TRAY_EXTENDED:   return UI_TEXT_DOCK;
    case APP_CH32_STAGE_WAITING_CARGO:   return UI_TEXT_PICKUP;
    case APP_CH32_STAGE_CARGO_DETECTED:  return UI_TEXT_PICKUP "OK";
    case APP_CH32_STAGE_TRAY_RETRACTING: return UI_TEXT_DOCK;
    case APP_CH32_STAGE_TRAY_RETRACTED:  return UI_TEXT_DOCK "OK";
    case APP_CH32_STAGE_DOOR_CLOSING:    return "\u95ED";
    case APP_CH32_STAGE_SAFE_LOCKED:     return UI_TEXT_DONE;
    case APP_CH32_STAGE_COMPLETE:        return UI_TEXT_DOCK_DONE;
    case APP_CH32_STAGE_FAULT:           return UI_TEXT_DOCK_ERROR;
    case APP_CH32_STAGE_READY:           return "READY";
    case APP_CH32_STAGE_IDLE:            return UI_TEXT_WAIT_DOCK;
    case APP_CH32_STAGE_UNKNOWN:
    default:                             return UI_TEXT_DOCK_WAIT;
    }
}

static void app_ctrl_fill_tag_flow(const app_ctrl_cycle_t *cycle,
    app_ui_flow_snapshot_t *flow)
{
    // 进入标签阶段说明无人机 AI 已确认。
    app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DRONE, "AI OK");

    const app_dock_judge_result_t *dock = &cycle->dock;
    if (!dock->vision_valid)
    {
        app_ctrl_flow_activate(flow,
            APP_UI_FLOW_STEP_TAG,
            UI_TEXT_WAIT_TAG,
            UI_TEXT_WAIT_TAG);
        return;
    }

    const int32_t distance_cm =
        dock->est_distance_mm > 0 ? (dock->est_distance_mm + 5) / 10 : -1;
    char tag_status[32] = {0};
    if (distance_cm > 0)
    {
        snprintf(tag_status, sizeof(tag_status), "Tag %ldcm", (long)distance_cm);
    }
    else
    {
        snprintf(tag_status, sizeof(tag_status), "Tag %u", (unsigned)dock->tag_id);
    }
    app_ctrl_flow_activate(flow,
        APP_UI_FLOW_STEP_TAG,
        distance_cm > 0 ? tag_status : UI_TEXT_WAIT_TAG,
        tag_status);
}

static void app_ctrl_fill_flow_snapshot(const app_ctrl_cycle_t *cycle,
    app_ui_flow_snapshot_t *flow)
{
    const app_task_snapshot_t *task = &cycle->task;
    const app_ctrl_runtime_view_t *runtime = &cycle->runtime;
    app_ctrl_flow_init(flow);
    if (!task->inited)
    {
        return;
    }

    if (task->state == APP_TASK_STATE_IDLE ||
        task->state == APP_TASK_STATE_CONFIGURED)
    {
        snprintf(flow->step_detail[APP_UI_FLOW_STEP_DRONE],
            sizeof(flow->step_detail[0]),
            "Tag %u",
            (unsigned)task->target_id);
        return;
    }

    if (task->state == APP_TASK_STATE_CANCELLED)
    {
        app_ctrl_flow_fail(flow,
            APP_UI_FLOW_STEP_DONE,
            "STOP",
            UI_TEXT_NOT_DONE);
        return;
    }

    // 故障优先显示，避免被普通机械阶段覆盖。
    if (task->state == APP_TASK_STATE_FAULT ||
        runtime->proto_error != APP_CH32_ERR_NONE)
    {
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DRONE, "AI OK");
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_TAG, "Tag OK");
        app_ctrl_flow_fail(flow,
            APP_UI_FLOW_STEP_EXEC,
            UI_TEXT_DOCK_ERROR,
            runtime->proto_error != APP_CH32_ERR_NONE ? "ERR" : UI_TEXT_DOCK_ERROR);
        return;
    }

    if (task->state == APP_TASK_STATE_COMPLETED)
    {
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DRONE, "AI OK");
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_TAG, "Tag OK");
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_EXEC, UI_TEXT_DOCK_DONE);
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DONE, UI_TEXT_DONE);
        flow->active_step = APP_UI_FLOW_STEP_DONE;
        strlcpy(flow->headline, UI_TEXT_DOCK_DONE, sizeof(flow->headline));
        return;
    }

    // 鉴权通过后立即显示执行阶段，不等待首个 CH32 busy 状态。
    if (runtime->dock_busy ||
        task->state == APP_TASK_STATE_DOCKING ||
        task->state == APP_TASK_STATE_AUTH_PASSED)
    {
        const char *action = app_ctrl_proto_stage_action_text(runtime->proto_stage);
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DRONE, "AI OK");
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_TAG, "Tag OK");
        app_ctrl_flow_activate(flow, APP_UI_FLOW_STEP_EXEC, action, action);
        return;
    }

    if (task->state != APP_TASK_STATE_WAIT_APPROACH)
    {
        return;
    }
    if (cycle->apriltag_enabled)
    {
        app_ctrl_fill_tag_flow(cycle, flow);
        return;
    }

    app_drone_ai_stats_t ai = {0};
    app_drone_ai_get_stats(&ai);
    const unsigned confirm_hits =
        (unsigned)(ai.confirm_hits != 0U ? ai.confirm_hits : 1U);
    char ai_status[32] = {0};
    snprintf(ai_status,
        sizeof(ai_status),
        "AI %u/%u",
        (unsigned)ai.hit_count,
        confirm_hits);
    app_ctrl_flow_activate(flow,
        APP_UI_FLOW_STEP_DRONE,
        ai_status,
        ai_status);
}

void app_ctrl_ui_publish(const app_ctrl_cycle_t *cycle)
{
    if (cycle == NULL)
    {
        return;
    }

    const app_ctrl_runtime_view_t *runtime = &cycle->runtime;
    if (runtime->weather_blocked)
    {
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
    }

    char status[128] = {0};
    char detail[224] = {0};
    char task_brief[96] = {0};
    app_task_format_brief(&cycle->task, task_brief, sizeof(task_brief));
    app_ctrl_compose_task_status(cycle, status, sizeof(status));
    app_ctrl_compose_detail(cycle, detail, sizeof(detail));

    if (cycle->task.active &&
        cycle->task.state == APP_TASK_STATE_WAIT_APPROACH &&
        !cycle->apriltag_enabled)
    {
        char ai_status[64] = {0};
        app_drone_ai_format_status(ai_status, sizeof(ai_status));
        snprintf(detail, sizeof(detail), "dock dbg: apriltag gated / %s", ai_status);
    }
    // 文案优先级：任务状态 < 机械状态 < 错误/冷却 < 临时提示。
    if (runtime->dock_busy)
    {
        strlcpy(status,
            app_ctrl_proto_stage_status_text(runtime->proto_stage),
            sizeof(status));
    }
    if (runtime->proto_error != APP_CH32_ERR_NONE && !runtime->dock_busy)
    {
        snprintf(status,
            sizeof(status),
            "dock: CH32 err %s",
            app_ch32_link_proto_error_name(runtime->proto_error));
    }
    if (runtime->retrigger_blocked &&
        !runtime->dock_busy &&
        runtime->proto_error == APP_CH32_ERR_NONE &&
        runtime->notice[0] == '\0')
    {
        strlcpy(status, "dock: cooldown / wait next approach", sizeof(status));
    }
    if (runtime->notice_active && runtime->notice[0] != '\0')
    {
        strlcpy(status, runtime->notice, sizeof(status));
    }

    // 一次提交所有内容，减少重复获取 LVGL 锁。
    app_ui_flow_snapshot_t flow = {0};
    app_ctrl_fill_flow_snapshot(cycle, &flow);
    app_ui_update_control_state(status,
        task_brief,
        detail,
        &cycle->vision,
        &cycle->dock,
        &flow);
}
