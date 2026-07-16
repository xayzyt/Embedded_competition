#include "app_ctrl_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_ch32_link.h"
#include "app_drone_ai.h"
#include "app_safety_takeover.h"
#include "app_ui.h"

// UI text is kept as escaped UTF-8 literals so this file remains ASCII-safe.
#define UI_TEXT_WAIT          "\u7B49\u5F85"
#define UI_TEXT_WAIT_CLOUD    "\u7B49\u5F85\u4E91\u7AEF"
#define UI_TEXT_WAIT_TAG      "\u7B49\u5F85Tag"
#define UI_TEXT_WAIT_DOCK     "\u7B49\u5F85\u63A5\u9A73"
#define UI_TEXT_NOT_DONE      "\u672A\u5B8C\u6210"
#define UI_TEXT_DOCK_DONE     "\u63A5\u9A73\u5B8C\u6210"
#define UI_TEXT_DOCK_ERROR    "\u63A5\u9A73ERR"
#define UI_TEXT_DOCK_WAIT     "\u63A5\u9A73\u7B49\u5F85"
#define UI_TEXT_DONE          "\u5B8C\u6210"
#define UI_TEXT_SYSTEM_READY  "\u7CFB\u7EDF\u5C31\u7EEA"
#define UI_TEXT_WAIT_DRONE    "\u7B49\u5F85\u65E0\u4EBA\u673A"
#define UI_TEXT_WAIT_CH32     "\u7B49\u5F85CH32"
#define UI_TEXT_IDENT_DRONE   "\u8BC6\u522B\u65E0\u4EBA\u673A"
#define UI_TEXT_DRONE_OK      "\u65E0\u4EBA\u673A\u5DF2\u8BC6\u522B"
#define UI_TEXT_IDENT_TAG     "\u8BC6\u522B\u8EAB\u4EFD"
#define UI_TEXT_SEARCH_TAG    "\u641C\u7D22\u6807\u7B7E"
#define UI_TEXT_ALIGN_TAG     "\u5BF9\u51C6\u6807\u7B7E"
#define UI_TEXT_STABLE_TAG    "\u7A33\u5B9A\u6807\u7B7E"
#define UI_TEXT_TAG_PASSED    "\u8EAB\u4EFD\u901A\u8FC7"
#define UI_TEXT_READY_DOCK    "\u51C6\u5907\u63A5\u9A73"
#define UI_TEXT_DOCK_START    "\u63A5\u9A73\u542F\u52A8"
#define UI_TEXT_FAULT         "\u7CFB\u7EDF\u5F02\u5E38"
#define UI_TEXT_STOPPED       "\u4EFB\u52A1\u505C\u6B62"
#define UI_TEXT_WEATHER       "\u5929\u6C14\u4FDD\u62A4"
#define UI_TEXT_VISION_OK     "\u89C6\u89C9\u5DF2\u901A\u8FC7"
#define UI_TEXT_WAIT_FRAME    "\u7B49\u5F85\u63A8\u7406\u5E27"
#define UI_TEXT_STAGE_IDLE    "\u673A\u68B0\u5F85\u673A"
#define UI_TEXT_DOOR_OPENING  "\u5916\u95E8\u6253\u5F00\u4E2D"
#define UI_TEXT_DOOR_OPENED   "\u5916\u95E8\u5DF2\u6253\u5F00"
#define UI_TEXT_TRAY_EXT      "\u6258\u76D8\u4F38\u51FA\u4E2D"
#define UI_TEXT_TRAY_EXTED    "\u6258\u76D8\u5DF2\u4F38\u51FA"
#define UI_TEXT_WAIT_CARGO    "\u7B49\u5F85\u653E\u8D27"
#define UI_TEXT_CARGO_OK      "\u8D27\u7269\u5DF2\u68C0\u6D4B"
#define UI_TEXT_TRAY_RET      "\u6258\u76D8\u56DE\u6536\u4E2D"
#define UI_TEXT_TRAY_RETED    "\u6258\u76D8\u5DF2\u56DE\u6536"
#define UI_TEXT_DOOR_CLOSING  "\u5916\u95E8\u5173\u95ED\u4E2D"
#define UI_TEXT_SAFE_LOCKED   "\u5B89\u5168\u9501\u6B62"
#define UI_TEXT_STAGE_FAULT   "\u673A\u68B0\u6545\u969C"

static unsigned app_ctrl_ai_confirm_total(const app_drone_ai_stats_t *ai)
{
    return (unsigned)((ai != NULL && ai->confirm_hits != 0U) ? ai->confirm_hits : 1U);
}

static unsigned app_ctrl_ai_hit_count(const app_drone_ai_stats_t *ai)
{
    const unsigned total = app_ctrl_ai_confirm_total(ai);
    unsigned hits = (unsigned)(ai != NULL ? ai->hit_count : 0U);
    return hits > total ? total : hits;
}

static void app_ctrl_build_cockpit_view(const app_ctrl_cycle_t *cycle,
    app_ui_cockpit_view_t *view)
{
    memset(view, 0, sizeof(*view));

    app_task_snapshot_t task = cycle->task;
    app_task_snapshot_t latest_task = {0};
    if (app_task_peek_snapshot(&latest_task) &&
        latest_task.generation == task.generation)
    {
        task = latest_task;
    }

    view->task_generation = task.generation;
    view->updated_ms = cycle->now_ms;
    view->target_id = task.target_id;
    view->task_active = task.active;
    view->vision = cycle->vision;
    view->dock = cycle->dock;
    view->weather_blocked = cycle->runtime.weather_blocked;

    app_drone_ai_snapshot_t ai = {0};
    const bool have_ai = app_drone_ai_get_snapshot(&ai);
    const bool task_advanced =
        task.state == APP_TASK_STATE_AUTH_PASSED ||
        task.state == APP_TASK_STATE_DOCKING;
    view->ai_valid = have_ai &&
        (task_advanced || ai.result_ms >= task.state_since_ms);
    view->ai_confirmed = task_advanced || (view->ai_valid && ai.confirmed);
    view->ai_drone_score = view->ai_valid ? ai.drone_score : 0.0f;
    view->ai_hit_count = view->ai_valid ? ai.hit_count : 0U;
    view->ai_confirm_hits = ai.confirm_hits != 0U ? ai.confirm_hits : 1U;
    view->delivery_permitted = task_advanced && !view->weather_blocked;

    app_dock_judge_config_t cfg = {0};
    if (app_dock_judge_get_config(&cfg))
    {
        view->use_distance_gate = cfg.use_distance_gate;
        view->min_distance_mm = cfg.min_distance_mm;
        view->max_distance_mm = cfg.max_distance_mm;
        view->center_x_tol = cfg.center_x_tol;
        view->center_y_tol = cfg.center_y_tol;
        view->stable_required = cfg.min_stable_count;
    }
    if (view->stable_required == 0U)
    {
        view->stable_required = 1U;
    }

    view->gate[0] = view->task_active ?
        APP_UI_COCKPIT_GATE_PASSED : APP_UI_COCKPIT_GATE_WAITING;
    view->gate[1] = view->ai_confirmed ?
        APP_UI_COCKPIT_GATE_PASSED :
        (view->task_active ? APP_UI_COCKPIT_GATE_ACTIVE : APP_UI_COCKPIT_GATE_WAITING);

    if (task_advanced)
    {
        view->gate[2] = APP_UI_COCKPIT_GATE_PASSED;
        view->gate[3] = APP_UI_COCKPIT_GATE_PASSED;
        view->gate[4] = view->weather_blocked ?
            APP_UI_COCKPIT_GATE_BLOCKED : APP_UI_COCKPIT_GATE_PASSED;
        return;
    }

    if (!view->ai_confirmed || !view->dock.vision_valid)
    {
        view->gate[2] = APP_UI_COCKPIT_GATE_WAITING;
    }
    else
    {
        view->gate[2] = view->dock.target_id_ok ?
            APP_UI_COCKPIT_GATE_PASSED : APP_UI_COCKPIT_GATE_BLOCKED;
    }

    const bool identity_passed =
        view->gate[2] == APP_UI_COCKPIT_GATE_PASSED;
    const bool position_passed = identity_passed &&
        view->dock.centered_ok &&
        view->dock.near_ok &&
        view->dock.distance_ok;
    view->gate[3] = !identity_passed ?
        APP_UI_COCKPIT_GATE_WAITING :
        (position_passed ? APP_UI_COCKPIT_GATE_PASSED : APP_UI_COCKPIT_GATE_ADJUST);

    if (view->weather_blocked)
    {
        view->gate[4] = APP_UI_COCKPIT_GATE_BLOCKED;
    }
    else if (!position_passed)
    {
        view->gate[4] = APP_UI_COCKPIT_GATE_WAITING;
    }
    else
    {
        view->gate[4] = view->dock.stable_ok ?
            APP_UI_COCKPIT_GATE_PASSED : APP_UI_COCKPIT_GATE_ACTIVE;
    }
}

static int32_t app_ctrl_distance_cm(const app_dock_judge_result_t *dock)
{
    if (dock == NULL || dock->est_distance_mm <= 0)
    {
        return -1;
    }
    return (dock->est_distance_mm + 5) / 10;
}

static const char *app_ctrl_proto_stage_action_text(app_ch32_proto_stage_t stage)
{
    switch (stage) {
    case APP_CH32_STAGE_IDLE:            return UI_TEXT_STAGE_IDLE;
    case APP_CH32_STAGE_READY:           return UI_TEXT_DOCK_START;
    case APP_CH32_STAGE_DOOR_OPENING:    return UI_TEXT_DOOR_OPENING;
    case APP_CH32_STAGE_DOOR_OPENED:     return UI_TEXT_DOOR_OPENED;
    case APP_CH32_STAGE_TRAY_EXTENDING:  return UI_TEXT_TRAY_EXT;
    case APP_CH32_STAGE_TRAY_EXTENDED:   return UI_TEXT_TRAY_EXTED;
    case APP_CH32_STAGE_WAITING_CARGO:   return UI_TEXT_WAIT_CARGO;
    case APP_CH32_STAGE_CARGO_DETECTED:  return UI_TEXT_CARGO_OK;
    case APP_CH32_STAGE_TRAY_RETRACTING: return UI_TEXT_TRAY_RET;
    case APP_CH32_STAGE_TRAY_RETRACTED:  return UI_TEXT_TRAY_RETED;
    case APP_CH32_STAGE_DOOR_CLOSING:    return UI_TEXT_DOOR_CLOSING;
    case APP_CH32_STAGE_SAFE_LOCKED:     return UI_TEXT_SAFE_LOCKED;
    case APP_CH32_STAGE_COMPLETE:        return UI_TEXT_DOCK_DONE;
    case APP_CH32_STAGE_FAULT:           return UI_TEXT_STAGE_FAULT;
    case APP_CH32_STAGE_UNKNOWN:
    default:                             return UI_TEXT_DOCK_START;
    }
}

static const char *app_ctrl_proto_stage_short_text(app_ch32_proto_stage_t stage)
{
    switch (stage) {
    case APP_CH32_STAGE_DOOR_OPENING:    return "\u5F00\u95E8\u4E2D";
    case APP_CH32_STAGE_DOOR_OPENED:     return "\u95E8\u5DF2\u5F00";
    case APP_CH32_STAGE_TRAY_EXTENDING:  return "\u4F38\u51FA\u4E2D";
    case APP_CH32_STAGE_TRAY_EXTENDED:   return "\u5DF2\u4F38\u51FA";
    case APP_CH32_STAGE_WAITING_CARGO:   return "\u7B49\u653E\u8D27";
    case APP_CH32_STAGE_CARGO_DETECTED:  return "\u8D27\u5DF2\u68C0";
    case APP_CH32_STAGE_TRAY_RETRACTING: return "\u56DE\u6536\u4E2D";
    case APP_CH32_STAGE_TRAY_RETRACTED:  return "\u5DF2\u56DE\u6536";
    case APP_CH32_STAGE_DOOR_CLOSING:    return "\u5173\u95E8\u4E2D";
    case APP_CH32_STAGE_SAFE_LOCKED:     return "\u5DF2\u9501\u6B62";
    case APP_CH32_STAGE_COMPLETE:        return "\u5DF2\u5B8C\u6210";
    case APP_CH32_STAGE_FAULT:           return "\u6545\u969C";
    case APP_CH32_STAGE_IDLE:            return "\u5F85\u673A";
    case APP_CH32_STAGE_READY:           return "\u542F\u52A8";
    case APP_CH32_STAGE_UNKNOWN:
    default:                             return "\u542F\u52A8";
    }
}

static void app_ctrl_format_ai_vision(char *buf, size_t buf_len)
{
    snprintf(buf,
        buf_len,
        "\u6BCF%u\u5E27\u63A8\u7406\u4E00\u6B21",
        (unsigned)APP_DRONE_AI_SAMPLE_INTERVAL_FRAMES);
}

static void app_ctrl_format_ai_detail(char *buf, size_t buf_len)
{
    app_drone_ai_stats_t ai = {0};
    app_drone_ai_get_stats(&ai);
    const unsigned total = app_ctrl_ai_confirm_total(&ai);
    const unsigned hits = app_ctrl_ai_hit_count(&ai);

    if (ai.confirmed)
    {
        strlcpy(buf,
            UI_TEXT_DRONE_OK ": \u51C6\u5907\u8BC6\u522B\u6807\u7B7E",
            buf_len);
    }
    else if (ai.inferred == 0U)
    {
        strlcpy(buf,
            UI_TEXT_IDENT_DRONE ": " UI_TEXT_WAIT_FRAME,
            buf_len);
    }
    else
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_IDENT_DRONE ": \u63A8\u7406%lu\u5E27 \u547D\u4E2D%u/%u",
            (unsigned long)ai.inferred,
            hits,
            total);
    }
}

static void app_ctrl_format_tag_vision(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_dock_judge_result_t *dock = &cycle->dock;
    if (!dock->vision_valid)
    {
        strlcpy(buf, UI_TEXT_SEARCH_TAG, buf_len);
    }
    else if (!dock->target_id_ok)
    {
        snprintf(buf, buf_len, "Tag%u ID\u9519", (unsigned)dock->tag_id);
    }
    else if (dock->state == APP_DOCK_STATE_READY_TO_DOCK)
    {
        snprintf(buf, buf_len, "Tag%u \u5DF2\u901A\u8FC7", (unsigned)dock->tag_id);
    }
    else if (!dock->centered_ok || !dock->near_ok)
    {
        snprintf(buf, buf_len, "Tag%u \u5BF9\u51C6\u4E2D", (unsigned)dock->tag_id);
    }
    else if (!dock->stable_ok)
    {
        snprintf(buf, buf_len, "\u7A33\u5B9A\u5E27%u", (unsigned)dock->stable_count);
    }
    else
    {
        snprintf(buf, buf_len, "Tag%u \u53EF\u63A5\u9A73", (unsigned)dock->tag_id);
    }
}

static void app_ctrl_format_tag_detail(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_dock_judge_result_t *dock = &cycle->dock;
    const uint16_t target_id = cycle->task.target_id;
    const int32_t distance_cm = app_ctrl_distance_cm(dock);

    if (!dock->vision_valid)
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_IDENT_TAG ": \u641C\u7D22\u76EE\u6807Tag%u",
            (unsigned)target_id);
    }
    else if (!dock->target_id_ok)
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_IDENT_TAG ": \u770B\u5230Tag%u \u7B49\u5F85Tag%u",
            (unsigned)dock->tag_id,
            (unsigned)target_id);
    }
    else if (!dock->centered_ok)
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_ALIGN_TAG ": Tag%u \u504F\u79FB%+ld,%+ld",
            (unsigned)dock->tag_id,
            (long)dock->dx,
            (long)dock->dy);
    }
    else if (!dock->near_ok)
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_ALIGN_TAG ": Tag%u \u9760\u8FD1\u4E2D \u504F\u79FB%+ld,%+ld",
            (unsigned)dock->tag_id,
            (long)dock->dx,
            (long)dock->dy);
    }
    else if (!dock->stable_ok)
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_STABLE_TAG ": Tag%u \u7A33\u5B9A\u5E27%u",
            (unsigned)dock->tag_id,
            (unsigned)dock->stable_count);
    }
    else if (!dock->distance_ok)
    {
        if (distance_cm > 0)
        {
            snprintf(buf,
                buf_len,
                "\u8DDD\u79BB\u6821\u9A8C: Tag%u %ldcm",
                (unsigned)dock->tag_id,
                (long)distance_cm);
        }
        else
        {
            strlcpy(buf,
                "\u8DDD\u79BB\u6821\u9A8C: \u7B49\u5F85\u6709\u6548\u8DDD\u79BB",
                buf_len);
        }
    }
    else
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_TAG_PASSED ": Tag%u \u53EF\u63A5\u9A73",
            (unsigned)dock->tag_id);
    }
}

static void app_ctrl_format_stage_detail(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const char *stage = app_ctrl_proto_stage_action_text(cycle->runtime.proto_stage);
    if (cycle->runtime.has_weight)
    {
        snprintf(buf,
            buf_len,
            "\u63A5\u9A73\u6267\u884C: %s \u91CD\u91CF%ldg",
            stage,
            (long)cycle->runtime.weight_g);
    }
    else
    {
        snprintf(buf, buf_len, "\u63A5\u9A73\u6267\u884C: %s", stage);
    }
}

static void __attribute__((unused)) app_ctrl_compose_detail(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_task_snapshot_t *task = &cycle->task;
    if (cycle->runtime.weather_blocked)
    {
        strlcpy(buf, UI_TEXT_WEATHER ": \u6682\u505C\u63A5\u9A73", buf_len);
    }
    else if (!task->active &&
        (task->state == APP_TASK_STATE_IDLE || task->state == APP_TASK_STATE_CONFIGURED))
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_WAIT_DRONE ": \u76EE\u6807Tag%u",
            (unsigned)task->target_id);
    }
    else if (task->state == APP_TASK_STATE_WAIT_APPROACH && !cycle->apriltag_enabled)
    {
        app_ctrl_format_ai_detail(buf, buf_len);
    }
    else if (task->state == APP_TASK_STATE_WAIT_APPROACH)
    {
        app_ctrl_format_tag_detail(cycle, buf, buf_len);
    }
    else if (task->state == APP_TASK_STATE_AUTH_PASSED ||
        task->state == APP_TASK_STATE_DOCKING ||
        cycle->runtime.dock_busy)
    {
        app_ctrl_format_stage_detail(cycle, buf, buf_len);
    }
    else if (task->state == APP_TASK_STATE_COMPLETED)
    {
        strlcpy(buf, UI_TEXT_DOCK_DONE ": \u5B89\u5168\u5F52\u4F4D", buf_len);
    }
    else if (task->state == APP_TASK_STATE_FAULT)
    {
        snprintf(buf,
            buf_len,
            UI_TEXT_FAULT ": %s",
            task->note[0] != '\0' ? task->note : "CH32");
    }
    else if (task->state == APP_TASK_STATE_CANCELLED)
    {
        strlcpy(buf, UI_TEXT_STOPPED, buf_len);
    }
    else
    {
        strlcpy(buf, UI_TEXT_SYSTEM_READY, buf_len);
    }
}

static void __attribute__((unused)) app_ctrl_compose_task_status(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_task_snapshot_t *task = &cycle->task;
    switch (task->state) {
    case APP_TASK_STATE_CONFIGURED:
        strlcpy(buf,
            cycle->runtime.ch32_ready ? UI_TEXT_WAIT_DRONE : UI_TEXT_WAIT_CH32,
            buf_len);
        break;

    case APP_TASK_STATE_WAIT_APPROACH:
        strlcpy(buf,
            cycle->apriltag_enabled ? UI_TEXT_IDENT_TAG : UI_TEXT_IDENT_DRONE,
            buf_len);
        break;

    case APP_TASK_STATE_AUTH_PASSED:
        strlcpy(buf, UI_TEXT_READY_DOCK, buf_len);
        break;

    case APP_TASK_STATE_DOCKING:
        strlcpy(buf,
            app_ctrl_proto_stage_action_text(cycle->runtime.proto_stage),
            buf_len);
        break;

    case APP_TASK_STATE_COMPLETED:
        strlcpy(buf, UI_TEXT_DOCK_DONE, buf_len);
        break;

    case APP_TASK_STATE_FAULT:
        strlcpy(buf, UI_TEXT_FAULT, buf_len);
        break;

    case APP_TASK_STATE_CANCELLED:
        strlcpy(buf, UI_TEXT_STOPPED, buf_len);
        break;

    case APP_TASK_STATE_IDLE:
    default:
        strlcpy(buf, UI_TEXT_SYSTEM_READY, buf_len);
        break;
    }
}

static void __attribute__((unused)) app_ctrl_compose_vision_status(const app_ctrl_cycle_t *cycle,
    char *buf,
    size_t buf_len)
{
    const app_task_snapshot_t *task = &cycle->task;
    if (task->state == APP_TASK_STATE_WAIT_APPROACH && !cycle->apriltag_enabled)
    {
        app_ctrl_format_ai_vision(buf, buf_len);
    }
    else if (task->state == APP_TASK_STATE_WAIT_APPROACH)
    {
        app_ctrl_format_tag_vision(cycle, buf, buf_len);
    }
    else if (task->state == APP_TASK_STATE_AUTH_PASSED ||
        task->state == APP_TASK_STATE_DOCKING ||
        task->state == APP_TASK_STATE_COMPLETED)
    {
        strlcpy(buf, UI_TEXT_VISION_OK, buf_len);
    }
    else if (task->state == APP_TASK_STATE_FAULT)
    {
        strlcpy(buf, UI_TEXT_FAULT, buf_len);
    }
    else
    {
        strlcpy(buf, UI_TEXT_WAIT, buf_len);
    }
}

static void app_ctrl_flow_set_detail(app_ui_flow_snapshot_t *flow,
    app_ui_flow_step_t step,
    const char *text)
{
    strlcpy(flow->step_detail[step], text, sizeof(flow->step_detail[0]));
}

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
    snprintf(flow->headline,
        sizeof(flow->headline),
        "\u5F53\u524D\uFF1A%s",
        headline);
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

    memset(flow, 0, sizeof(*flow));
    flow->active_step = APP_UI_FLOW_STEP_DRONE;
    strlcpy(flow->headline, UI_TEXT_WAIT_CLOUD, sizeof(flow->headline));
    for (int i = 0; i < APP_UI_FLOW_STEP_COUNT; i++)
    {
        flow->step_state[i] = APP_UI_FLOW_STATE_WAITING;
        app_ctrl_flow_set_detail(flow, (app_ui_flow_step_t)i, details[i]);
    }
}

static void app_ctrl_fill_tag_flow(const app_ctrl_cycle_t *cycle,
    app_ui_flow_snapshot_t *flow)
{
    app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DRONE, "AI OK");

    const app_dock_judge_result_t *dock = &cycle->dock;
    if (!dock->vision_valid)
    {
        app_ctrl_flow_activate(flow,
            APP_UI_FLOW_STEP_TAG,
            UI_TEXT_SEARCH_TAG,
            UI_TEXT_WAIT);
        return;
    }
    if (!dock->target_id_ok)
    {
        app_ctrl_flow_activate(flow,
            APP_UI_FLOW_STEP_TAG,
            UI_TEXT_IDENT_TAG,
            "ID\u9519");
        return;
    }
    if (!dock->centered_ok || !dock->near_ok)
    {
        app_ctrl_flow_activate(flow,
            APP_UI_FLOW_STEP_TAG,
            UI_TEXT_ALIGN_TAG,
            "\u5BF9\u51C6\u4E2D");
        return;
    }
    if (!dock->stable_ok)
    {
        app_ctrl_flow_activate(flow,
            APP_UI_FLOW_STEP_TAG,
            UI_TEXT_STABLE_TAG,
            "\u7A33\u5B9A\u4E2D");
        return;
    }
    if (dock->state == APP_DOCK_STATE_READY_TO_DOCK)
    {
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_TAG, "Tag OK");
        flow->active_step = APP_UI_FLOW_STEP_TAG;
        snprintf(flow->headline,
            sizeof(flow->headline),
            "\u5F53\u524D\uFF1A%s",
            UI_TEXT_TAG_PASSED);
        return;
    }

    char tag_status[32] = {0};
    snprintf(tag_status, sizeof(tag_status), "Tag%u", (unsigned)dock->tag_id);
    app_ctrl_flow_activate(flow,
        APP_UI_FLOW_STEP_TAG,
        UI_TEXT_IDENT_TAG,
        tag_status);
}

static void __attribute__((unused)) app_ctrl_fill_flow_snapshot(const app_ctrl_cycle_t *cycle,
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
        snprintf(flow->headline,
            sizeof(flow->headline),
            "\u5F53\u524D\uFF1A%s",
            UI_TEXT_WAIT_DRONE);
        snprintf(flow->step_detail[APP_UI_FLOW_STEP_DRONE],
            sizeof(flow->step_detail[0]),
            "\u76EE\u6807%u",
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

    if (runtime->dock_busy ||
        task->state == APP_TASK_STATE_DOCKING ||
        task->state == APP_TASK_STATE_AUTH_PASSED)
    {
        const char *action = app_ctrl_proto_stage_action_text(runtime->proto_stage);
        const char *short_action = app_ctrl_proto_stage_short_text(runtime->proto_stage);
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_DRONE, "AI OK");
        app_ctrl_flow_complete(flow, APP_UI_FLOW_STEP_TAG, "Tag OK");
        app_ctrl_flow_activate(flow, APP_UI_FLOW_STEP_EXEC, action, short_action);
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
    const unsigned confirm_hits = app_ctrl_ai_confirm_total(&ai);
    char ai_status[32] = {0};
    snprintf(ai_status,
        sizeof(ai_status),
        "\u547D\u4E2D%u/%u",
        app_ctrl_ai_hit_count(&ai),
        confirm_hits);
    app_ctrl_flow_activate(flow,
        APP_UI_FLOW_STEP_DRONE,
        UI_TEXT_IDENT_DRONE,
        ai_status);
}

void app_ctrl_ui_publish(const app_ctrl_cycle_t *cycle)
{
    if (app_safety_takeover_is_active())
    {
        return;
    }
    if (cycle == NULL)
    {
        return;
    }

    const app_ctrl_runtime_view_t *runtime = &cycle->runtime;
    if (runtime->weather_blocked)
    {
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
    }

    app_ui_cockpit_view_t cockpit = {0};
    app_ctrl_build_cockpit_view(cycle, &cockpit);
    app_ui_update_cockpit(&cockpit);
}
