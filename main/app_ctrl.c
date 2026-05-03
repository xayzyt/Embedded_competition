/*
 * app_ctrl.c - 接驳主控制状态机模块
 *
 * 这个文件是 SkyAnchor 的“业务大脑”之一，负责把视觉判定、任务状态、CH32 执行反馈串成完整流程。
 * 典型链路是：
 * 1. app_dock_judge 判断无人机已经对准且允许接驳；
 * 2. app_ctrl 下发 START_DOCK / OPEN / EXTEND 等命令给 CH32；
 * 3. CH32 执行舱门、托盘、称重、回收、上锁，并不断回传状态；
 * 4. app_ctrl 根据 CH32 状态更新 UI、任务状态和云端快照；
 * 5. 异常时进入冷却、保护或等待人工处理。
 *
 * 这个模块最需要注意的是“防重复触发”：识别结果每帧都会更新，如果不加冷却和状态门控，
 * 可能会反复给 CH32 下发开舱命令。因此文件里有 retrigger cooldown、busy deadline、cargo wait window 等保护逻辑。
 */

#include "app_ctrl.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_ch32_link.h"
#include "app_dock_judge.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"

static const char *TAG = "app_ctrl";

/* -------------------------------------------------------------------------- */
/* 时序和任务配置                                               */
/* -------------------------------------------------------------------------- */

#define CTRL_TASK_STACK_SIZE            (7 * 1024)
#define CTRL_TASK_PRIORITY              5
#define CTRL_TASK_CORE_ID               1
#define CTRL_POLL_MS                    60U
#define CTRL_READY_PROBE_INTERVAL_MS    1000U
#define CTRL_DOCK_CMD                   ('A')
#define CTRL_ACK_WAIT_MS                2000U
#define CTRL_BUSY_TIMEOUT_MS            20000U
#define CTRL_NOTICE_SHOW_MS             1600U
#define CTRL_RETRIGGER_COOLDOWN_MS      1800U
#define CTRL_AUTO_DOCK_ENABLE           (1)

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool inited;                                /* 控制模块是否已经初始化。 */
    bool ch32_ready;                            /* CH32 链路最近是否处于 ready 状态。 */
    bool dock_busy;                             /* 接驳执行链路是否正在忙碌。 */
    bool cargo_wait_window_seen;                /* 本轮接驳是否已经进入过等待放货窗口。 */
    bool has_weight;                            /* 是否收到过有效称重数据。 */
    int32_t last_weight_g;                      /* 最近一次称重值，单位为克。 */
    uint16_t last_proto_flags;                  /* 最近一次 CH32 状态帧 flags。 */
    app_ch32_proto_stage_t last_proto_stage;    /* 最近一次 CH32 上报的阶段。 */
    uint8_t last_proto_error;                   /* 最近一次 CH32 上报的错误码。 */
    uint16_t applied_target_id;                 /* 已同步给接驳判定模块的目标 tag ID。 */
    uint32_t last_ready_probe_ms;               /* 最近一次主动探测 CH32 ready 的时间。 */
    uint32_t busy_deadline_ms;                  /* 接驳忙碌阶段的超时截止时间。 */
    uint32_t notice_deadline_ms;                /* 临时 UI 提示的显示截止时间。 */
    uint32_t retrigger_deadline_ms;             /* 防重复触发冷却截止时间。 */
    char notice[96];                            /* 当前临时 UI 提示文本。 */
} app_ctrl_runtime_t;

/* 主循环每轮先拷贝一份共享状态，后续判断尽量在临界区外完成，避免阻塞 CH32 回调。 */
typedef struct {
    bool ch32_ready;                            /* 本轮循环看到的 CH32 ready 状态。 */
    bool dock_busy;                             /* 本轮循环使用的接驳忙碌状态。 */
    bool cargo_wait_window_seen;                /* 本轮循环使用的等待放货窗口标记。 */
    bool has_weight;                            /* 本轮循环是否有有效称重数据。 */
    int32_t weight_g;                           /* 本轮循环使用的称重值。 */
    uint16_t proto_flags;                       /* 本轮循环使用的 CH32 flags。 */
    app_ch32_proto_stage_t proto_stage;         /* 本轮循环使用的 CH32 阶段。 */
    uint8_t proto_error;                        /* 本轮循环使用的 CH32 错误码。 */
    uint16_t applied_target_id;                 /* 本轮循环读取到的已应用目标 ID。 */
    uint32_t last_probe_ms;                     /* 本轮循环读取到的最近 ready 探测时间。 */
    uint32_t busy_deadline_ms;                  /* 本轮循环读取到的忙碌超时截止时间。 */
    uint32_t notice_deadline_ms;                /* 本轮循环读取到的提示截止时间。 */
    uint32_t retrigger_deadline_ms;             /* 本轮循环读取到的冷却截止时间。 */
    char notice[96];                            /* 本轮循环读取到的提示文本。 */
} app_ctrl_loop_state_t;

static TaskHandle_t s_ctrl_task = NULL;
static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;
static app_ctrl_runtime_t s_rt = {0};

/* -------------------------------------------------------------------------- */
/* 小工具函数                                                             */
/* -------------------------------------------------------------------------- */

static inline uint32_t app_ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
static inline bool app_ctrl_deadline_active(uint32_t deadline_ms, uint32_t now_ms)
{
    return (deadline_ms != 0U) && ((int32_t)(deadline_ms - now_ms) > 0);
}
static void app_ctrl_set_notice_locked(const char *text, uint32_t hold_ms)
{
    if (text == NULL)
    {
        s_rt.notice[0] = '\0';
        s_rt.notice_deadline_ms = 0;
        return;
    }
    strlcpy(s_rt.notice, text, sizeof(s_rt.notice));
    s_rt.notice_deadline_ms = app_ctrl_now_ms() + hold_ms;
}
static void app_ctrl_set_notice(const char *text, uint32_t hold_ms)
{

    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_set_notice_locked(text, hold_ms);

    taskEXIT_CRITICAL(&s_ctrl_mux);
}
static void app_ctrl_start_retrigger_cooldown_locked(uint32_t hold_ms)
{
    s_rt.retrigger_deadline_ms = app_ctrl_now_ms() + hold_ms;
}

/* -------------------------------------------------------------------------- */
/* CH32 协议状态辅助函数                                                 */
/* -------------------------------------------------------------------------- */

static bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage)
{
    switch (stage) {
    case APP_CH32_STAGE_DOOR_OPENING:
    case APP_CH32_STAGE_DOOR_OPENED:
    case APP_CH32_STAGE_TRAY_EXTENDING:
    case APP_CH32_STAGE_TRAY_EXTENDED:
    case APP_CH32_STAGE_WAITING_CARGO:
    case APP_CH32_STAGE_CARGO_DETECTED:
    case APP_CH32_STAGE_TRAY_RETRACTING:
    case APP_CH32_STAGE_TRAY_RETRACTED:
    case APP_CH32_STAGE_DOOR_CLOSING:
        return true;
    default:
        return false;
    }
}
static bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage)
{
    return (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
    (stage == APP_CH32_STAGE_WAITING_CARGO);
}
static bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags)
{
    return (flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U;
}
static bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error)
{
    return (proto_error == APP_CH32_ERR_TIMEOUT) ||
    (proto_error == APP_CH32_ERR_WEIGHT);
}
static const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage)
{
    switch (stage) {
    case APP_CH32_STAGE_IDLE:            return "dock: CH32 idle";
    case APP_CH32_STAGE_READY:           return "dock: CH32 ready";
    case APP_CH32_STAGE_DOOR_OPENING:    return "dock: door opening";
    case APP_CH32_STAGE_DOOR_OPENED:     return "dock: door opened";
    case APP_CH32_STAGE_TRAY_EXTENDING:  return "dock: tray extending";
    case APP_CH32_STAGE_TRAY_EXTENDED:   return "dock: tray extended";
    case APP_CH32_STAGE_WAITING_CARGO:   return "dock: waiting cargo";
    case APP_CH32_STAGE_CARGO_DETECTED:  return "dock: cargo detected";
    case APP_CH32_STAGE_TRAY_RETRACTING: return "dock: tray retracting";
    case APP_CH32_STAGE_TRAY_RETRACTED:  return "dock: tray retracted";
    case APP_CH32_STAGE_DOOR_CLOSING:    return "dock: door closing";
    case APP_CH32_STAGE_SAFE_LOCKED:     return "dock: safe locked";
    case APP_CH32_STAGE_COMPLETE:        return "dock: cycle complete";
    case APP_CH32_STAGE_FAULT:           return "dock: CH32 fault";
    case APP_CH32_STAGE_UNKNOWN:
    default:                             return "dock: CH32 online";
    }
}
static bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage)
{
    return !app_ctrl_proto_stage_is_cargo_wait_window(stage);
}
static bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
    uint16_t prev_flags,
    app_ch32_proto_stage_t stage,
    uint16_t flags,
    uint8_t proto_error,
    bool cargo_wait_window_seen)
{
    const bool tray_out_now = app_ctrl_proto_flags_indicate_tray_out(flags);
    const bool tray_out_before = app_ctrl_proto_flags_indicate_tray_out(prev_flags);
    if (!app_ctrl_proto_error_is_cargo_wait_soft(proto_error))
    {
        return false;
    }
    if ((stage == APP_CH32_STAGE_FAULT) ||
        (stage == APP_CH32_STAGE_SAFE_LOCKED) ||
        (stage == APP_CH32_STAGE_COMPLETE) ||
        ((flags & APP_CH32_FLAG_LOCKED) != 0U))
    {
        return false;
    }
    if (cargo_wait_window_seen)
    {
        return app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        tray_out_now ||
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage) ||
        tray_out_before;
    }
    if (app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage))
    {
        return true;
    }
    return tray_out_now &&
    ((stage == APP_CH32_STAGE_TRAY_EXTENDING) ||
        (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
        (stage == APP_CH32_STAGE_WAITING_CARGO));
}
static void app_ctrl_hold_waiting_cargo_locked(void)
{
    s_rt.cargo_wait_window_seen = true;
    s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.dock_busy = true;
    s_rt.busy_deadline_ms = 0;
    app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
}

/* -------------------------------------------------------------------------- */
/* UI 文本拼装                                                         */
/* -------------------------------------------------------------------------- */

static void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
    bool has_weight,
    int32_t weight_g,
    app_ch32_proto_stage_t proto_stage,
    char *buf,
    size_t buf_len)
{
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U))
    {
        return;
    }
    if (!dock->vision_valid)
    {
        if (dock->state != APP_DOCK_STATE_SEARCHING)
        {
            snprintf(buf,
                buf_len,
                "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ldmm e:%.1f stage:%s",
                (unsigned)dock->invalid_hold_count,
                (unsigned)dock->lost_count,
                (long)dock->dx,
                (long)dock->dy,
                (long)dock->est_distance_mm,
                (double)dock->filtered_edge_px,
                app_ch32_link_proto_stage_name(proto_stage));
        }
        else
        {
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        }
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
        has_weight ? "" : "-",
        has_weight ? (long)weight_g : 0L);
}
static void app_ctrl_compose_guidance(const app_dock_judge_result_t *dock,
    char *buf,
    size_t buf_len)
{
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U))
    {
        return;
    }
    if (!dock->vision_valid)
    {
        snprintf(buf, buf_len, "dock: searching target");
        return;
    }
    if (!dock->target_id_ok)
    {
        snprintf(buf, buf_len, "dock: wrong tag id");
        return;
    }
    if (!dock->centered_ok)
    {
        snprintf(buf, buf_len, "dock: align target center");
        return;
    }
    if (!dock->near_ok)
    {
        snprintf(buf, buf_len, "dock: move target closer");
        return;
    }
    if (!dock->stable_ok)
    {
        snprintf(buf, buf_len, "dock: hold hover stable");
        return;
    }
    if (!dock->distance_ok)
    {
        if (dock->est_distance_mm > 0)
        {
            snprintf(buf,
                buf_len,
                (dock->est_distance_mm < 260) ? "dock: target too near" : "dock: target too far");
        }
        else
        {
            snprintf(buf, buf_len, "dock: wait valid distance");
        }
        return;
    }
    app_dock_judge_format_status(dock, buf, buf_len);
}
static void app_ctrl_compose_task_status(const app_task_snapshot_t *task,
    const app_dock_judge_result_t *dock,
    bool ch32_ready,
    char *buf,
    size_t buf_len)
{
    if (buf == NULL || buf_len == 0U || task == NULL)
    {
        return;
    }
    switch (task->state) {
    case APP_TASK_STATE_CONFIGURED:
        snprintf(buf,
            buf_len,
            ch32_ready ? "task: target=%u / remote ready" : "task: target=%u / wait CH32",
            (unsigned)task->target_id);
        break;
    case APP_TASK_STATE_WAIT_APPROACH: {
            char guide[72] = {0};
            app_ctrl_compose_guidance(dock, guide, sizeof(guide));
            snprintf(buf, buf_len, "task: wait id=%u / %s", (unsigned)task->target_id, guide);
            break;
        }
    case APP_TASK_STATE_AUTH_PASSED:
        snprintf(buf,
            buf_len,
            "task: auth passed / matched id=%u",
            (unsigned)(task->matched_tag_id != 0U ? task->matched_tag_id : task->target_id));
        break;
    case APP_TASK_STATE_DOCKING:
        snprintf(buf, buf_len, "task: docking in progress");
        break;
    case APP_TASK_STATE_COMPLETED:
        snprintf(buf,
            buf_len,
            "task: completed / target=%u",
            (unsigned)task->target_id);
        break;
    case APP_TASK_STATE_FAULT:
        snprintf(buf,
            buf_len,
            "task: fault / %s",
            task->note[0] != '\0' ? task->note : "check CH32");
        break;
    case APP_TASK_STATE_CANCELLED:
        snprintf(buf, buf_len, "task: cancelled / target=%u", (unsigned)task->target_id);
        break;
    case APP_TASK_STATE_IDLE:
    default:
        snprintf(buf, buf_len, "task: idle");
        break;
    }
}
static void app_ctrl_apply_proto_msg_locked(const app_ch32_line_t *msg)
{
    if (msg == NULL)
    {
        return;
    }
    const app_ch32_proto_stage_t prev_proto_stage = s_rt.last_proto_stage;
    const uint16_t prev_proto_flags = s_rt.last_proto_flags;
    const bool prev_cargo_wait_window_seen = s_rt.cargo_wait_window_seen;
    s_rt.ch32_ready = app_ch32_link_is_ready();
    if (msg->payload_len >= 8U)
    {
        s_rt.last_weight_g = msg->proto_weight_g;
        s_rt.has_weight = true;
        s_rt.last_proto_flags = msg->proto_flags;
    }
    if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
        (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
        (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
        (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT))
    {
        if (msg->payload_len >= 8U)
        {
            s_rt.last_proto_flags = msg->proto_flags;
        }
        if (app_ctrl_proto_stage_is_cargo_wait_window(msg->proto_stage) ||
            ((msg->payload_len >= 8U) && app_ctrl_proto_flags_indicate_tray_out(msg->proto_flags)))
        {
            s_rt.cargo_wait_window_seen = true;
        }
        if (s_rt.last_proto_stage != msg->proto_stage)
        {
            s_rt.last_proto_stage = msg->proto_stage;
            app_ctrl_set_notice_locked(app_ctrl_proto_stage_status_text(msg->proto_stage), CTRL_NOTICE_SHOW_MS);
        }
        if ((msg->type == APP_CH32_LINE_PROTO_ERROR) || (msg->proto_stage == APP_CH32_STAGE_FAULT))
        {
            if (app_ctrl_is_soft_waiting_cargo_error(prev_proto_stage,
                prev_proto_flags,
                msg->proto_stage,
                msg->proto_flags,
                msg->proto_detail,
                prev_cargo_wait_window_seen))
            {
                app_ctrl_hold_waiting_cargo_locked();
                return;
            }
            s_rt.last_proto_error = msg->proto_detail;
            s_rt.dock_busy = false;
            s_rt.cargo_wait_window_seen = false;
            s_rt.busy_deadline_ms = 0;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
            snprintf(s_rt.notice,
                sizeof(s_rt.notice),
                "dock: CH32 err %s",
                app_ch32_link_proto_error_name(msg->proto_detail));
            s_rt.notice_deadline_ms = app_ctrl_now_ms() + CTRL_NOTICE_SHOW_MS;
            return;
        }
        if (((msg->proto_flags & APP_CH32_FLAG_BUSY) != 0U) || app_ctrl_proto_stage_is_busy(msg->proto_stage))
        {
            s_rt.dock_busy = true;
            if (app_ctrl_proto_stage_uses_busy_deadline(msg->proto_stage))
            {
                s_rt.busy_deadline_ms = app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS;
            }
            else
            {
                s_rt.busy_deadline_ms = 0;
            }
            return;
        }
        if ((msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED) ||
            (msg->proto_stage == APP_CH32_STAGE_COMPLETE) ||
            (msg->proto_stage == APP_CH32_STAGE_IDLE) ||
            (msg->proto_stage == APP_CH32_STAGE_READY))
        {
            s_rt.dock_busy = false;
            s_rt.cargo_wait_window_seen = false;
            s_rt.busy_deadline_ms = 0;
            s_rt.last_proto_error = APP_CH32_ERR_NONE;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 控制循环辅助函数                                                        */
/* -------------------------------------------------------------------------- */

static void app_ctrl_read_loop_state(app_ctrl_loop_state_t *out)
{
    if (out == NULL)
    {
        return;
    }

    memset(out, 0, sizeof(*out));

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.ch32_ready = app_ch32_link_is_ready();
    out->ch32_ready = s_rt.ch32_ready;
    out->dock_busy = s_rt.dock_busy;
    out->cargo_wait_window_seen = s_rt.cargo_wait_window_seen;
    out->has_weight = s_rt.has_weight;
    out->weight_g = s_rt.last_weight_g;
    out->proto_stage = s_rt.last_proto_stage;
    out->proto_flags = s_rt.last_proto_flags;
    out->proto_error = s_rt.last_proto_error;
    out->last_probe_ms = s_rt.last_ready_probe_ms;
    out->busy_deadline_ms = s_rt.busy_deadline_ms;
    out->notice_deadline_ms = s_rt.notice_deadline_ms;
    out->retrigger_deadline_ms = s_rt.retrigger_deadline_ms;
    out->applied_target_id = s_rt.applied_target_id;
    strlcpy(out->notice, s_rt.notice, sizeof(out->notice));
    taskEXIT_CRITICAL(&s_ctrl_mux);
}

static void app_ctrl_apply_task_target_if_needed(const app_task_snapshot_t *task,
    uint16_t applied_target_id)
{
    if (task == NULL)
    {
        return;
    }
    if ((!task->target_dirty) && (applied_target_id == task->target_id))
    {
        return;
    }
    if (app_dock_judge_set_target_id(task->target_id, true) != ESP_OK)
    {
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.applied_target_id = task->target_id;
    taskEXIT_CRITICAL(&s_ctrl_mux);

    ESP_LOGI(TAG, "applied target id => %u", (unsigned)task->target_id);
}

static void app_ctrl_probe_ready_if_needed(uint32_t now_ms,
    bool ch32_ready,
    uint32_t last_probe_ms)
{
    if (ch32_ready || ((now_ms - last_probe_ms) < CTRL_READY_PROBE_INTERVAL_MS))
    {
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_ready_probe_ms = now_ms;
    taskEXIT_CRITICAL(&s_ctrl_mux);

    (void)app_ch32_link_probe_ready(200);
}

static void app_ctrl_set_waiting_cargo_local(bool *dock_busy,
    bool *cargo_wait_window_seen,
    app_ch32_proto_stage_t *proto_stage,
    uint8_t *proto_error,
    uint32_t *busy_deadline_ms)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_hold_waiting_cargo_locked();
    taskEXIT_CRITICAL(&s_ctrl_mux);

    if (dock_busy != NULL)
    {
        *dock_busy = true;
    }
    if (cargo_wait_window_seen != NULL)
    {
        *cargo_wait_window_seen = true;
    }
    if (proto_stage != NULL)
    {
        *proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    }
    if (proto_error != NULL)
    {
        *proto_error = APP_CH32_ERR_NONE;
    }
    if (busy_deadline_ms != NULL)
    {
        *busy_deadline_ms = 0;
    }
}

static void app_ctrl_handle_busy_timeout(uint32_t now_ms,
    app_task_snapshot_t *task,
    bool *dock_busy,
    bool *cargo_wait_window_seen,
    app_ch32_proto_stage_t *proto_stage,
    uint8_t *proto_error,
    uint32_t *busy_deadline_ms)
{
    if ((dock_busy == NULL) || (cargo_wait_window_seen == NULL) ||
        (proto_stage == NULL) || (proto_error == NULL) || (busy_deadline_ms == NULL))
    {
        return;
    }
    if ((!*dock_busy) || (*busy_deadline_ms == 0U) ||
        ((int32_t)(now_ms - *busy_deadline_ms) < 0))
    {
        return;
    }
    if (app_ctrl_proto_stage_is_cargo_wait_window(*proto_stage) || *cargo_wait_window_seen)
    {
        app_ctrl_set_waiting_cargo_local(dock_busy,
            cargo_wait_window_seen,
            proto_stage,
            proto_error,
            busy_deadline_ms);
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.dock_busy = false;
    s_rt.busy_deadline_ms = 0;
    s_rt.last_proto_error = APP_CH32_ERR_TIMEOUT;
    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
    app_ctrl_set_notice_locked("dock: CH32 timeout", CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);

    *dock_busy = false;
    *proto_error = APP_CH32_ERR_TIMEOUT;

    if ((task != NULL) && (task->active || task->state == APP_TASK_STATE_DOCKING))
    {
        app_task_mark_fault("CH32 timeout");
        (void)app_task_get_snapshot(task);
    }
}

static void app_ctrl_hold_soft_cargo_wait_if_needed(bool *dock_busy,
    bool *cargo_wait_window_seen,
    app_ch32_proto_stage_t *proto_stage,
    uint16_t proto_flags,
    uint8_t *proto_error,
    uint32_t *busy_deadline_ms)
{
    if ((dock_busy == NULL) || (cargo_wait_window_seen == NULL) ||
        (proto_stage == NULL) || (proto_error == NULL))
    {
        return;
    }
    if (*dock_busy)
    {
        return;
    }
    if (!app_ctrl_is_soft_waiting_cargo_error(*proto_stage,
            proto_flags,
            *proto_stage,
            proto_flags,
            *proto_error,
            *cargo_wait_window_seen))
    {
        return;
    }

    app_ctrl_set_waiting_cargo_local(dock_busy,
        cargo_wait_window_seen,
        proto_stage,
        proto_error,
        busy_deadline_ms);
}

static void app_ctrl_mark_auth_if_ready(app_task_snapshot_t *task,
    const app_dock_judge_result_t *dock,
    bool ready_level)
{
    if ((task == NULL) || (dock == NULL))
    {
        return;
    }
    if (task->active && (task->state == APP_TASK_STATE_WAIT_APPROACH) && ready_level)
    {
        app_task_mark_auth_passed(dock->tag_id);
        (void)app_task_get_snapshot(task);
        app_ctrl_set_notice("auth passed / ready to dock", CTRL_NOTICE_SHOW_MS);
    }
}

static void app_ctrl_try_auto_dock(uint32_t now_ms,
    const app_dock_judge_result_t *dock,
    app_task_snapshot_t *task,
    bool ch32_ready,
    bool retrigger_blocked,
    bool prev_ready_level,
    bool ready_level,
    bool *dock_busy,
    bool *cargo_wait_window_seen)
{
#if CTRL_AUTO_DOCK_ENABLE
    if ((dock == NULL) || (task == NULL) || (dock_busy == NULL) || (cargo_wait_window_seen == NULL))
    {
        return;
    }
    if (!(task->active && !*dock_busy && !retrigger_blocked && !prev_ready_level && ready_level))
    {
        return;
    }
    if (!ch32_ready)
    {
        app_ctrl_set_notice("dock: ready but CH32 not ready", CTRL_NOTICE_SHOW_MS);
        return;
    }

    ESP_LOGI(TAG,
        "READY rising edge -> send CH32 cmd %c (id=%u dx=%ld dy=%ld z=%ld score=%u)",
        CTRL_DOCK_CMD,
        (unsigned)dock->tag_id,
        (long)dock->dx,
        (long)dock->dy,
        (long)dock->est_distance_mm,
        (unsigned)dock->hover_score);

    esp_err_t ret = app_ch32_link_send_cmd_and_wait_ack(CTRL_DOCK_CMD, CTRL_ACK_WAIT_MS);
    if (ret == ESP_OK)
    {
        taskENTER_CRITICAL(&s_ctrl_mux);
        s_rt.dock_busy = true;
        s_rt.cargo_wait_window_seen = false;
        s_rt.last_proto_error = APP_CH32_ERR_NONE;
        s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
        s_rt.last_proto_flags = 0;
        s_rt.busy_deadline_ms = now_ms + CTRL_BUSY_TIMEOUT_MS;
        app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
        app_ctrl_set_notice_locked("dock: CH32 accepted start dock", CTRL_NOTICE_SHOW_MS);
        taskEXIT_CRITICAL(&s_ctrl_mux);

        *dock_busy = true;
        *cargo_wait_window_seen = false;
        app_task_mark_docking_started();
        (void)app_task_get_snapshot(task);
        return;
    }

    ESP_LOGW(TAG, "send CH32 cmd %c failed: %s", CTRL_DOCK_CMD, esp_err_to_name(ret));
    const bool ch32_rejected = (ret == ESP_ERR_INVALID_RESPONSE);

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_proto_error = APP_CH32_ERR_INTERNAL;
    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);

    app_ctrl_set_notice(ch32_rejected ? "dock: CH32 rejected cmd" : "dock: CH32 ack timeout",
        CTRL_NOTICE_SHOW_MS);
    app_task_mark_fault(ch32_rejected ? "CH32 rejected cmd" : "CH32 ack timeout");
    (void)app_task_get_snapshot(task);
#else
    (void)now_ms;
    (void)dock;
    (void)task;
    (void)ch32_ready;
    (void)retrigger_blocked;
    (void)prev_ready_level;
    (void)ready_level;
    (void)dock_busy;
    (void)cargo_wait_window_seen;
#endif
}

static void app_ctrl_update_task_for_busy_transition(bool prev_dock_busy,
    bool dock_busy,
    uint8_t proto_error,
    app_task_snapshot_t *task)
{
    if (task == NULL)
    {
        return;
    }
    if (!prev_dock_busy && dock_busy && task->active && task->state != APP_TASK_STATE_DOCKING)
    {
        app_task_mark_docking_started();
        (void)app_task_get_snapshot(task);
    }
    if (prev_dock_busy && !dock_busy)
    {
        if (proto_error == APP_CH32_ERR_NONE)
        {
            if (task->state == APP_TASK_STATE_DOCKING || task->active)
            {
                app_task_mark_completed("dock cycle done");
                (void)app_task_get_snapshot(task);
            }
        }
        else
        {
            app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));
            (void)app_task_get_snapshot(task);
        }
    }
    if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy &&
        (task->active || task->state == APP_TASK_STATE_DOCKING))
    {
        app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));
        (void)app_task_get_snapshot(task);
    }
}

static void app_ctrl_publish_ui(uint32_t now_ms,
    const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock,
    const app_task_snapshot_t *task,
    bool ch32_ready,
    bool dock_busy,
    bool has_weight,
    int32_t weight_g,
    app_ch32_proto_stage_t proto_stage,
    uint8_t proto_error,
    bool retrigger_blocked,
    uint32_t notice_deadline_ms,
    const char *notice)
{
    char status[128] = {0};
    char detail[224] = {0};
    char task_brief[96] = {0};

    app_task_format_brief(task, task_brief, sizeof(task_brief));
    app_ctrl_compose_task_status(task, dock, ch32_ready, status, sizeof(status));
    app_ctrl_compose_detail(dock, has_weight, weight_g, proto_stage, detail, sizeof(detail));

    if (dock_busy)
    {
        snprintf(status, sizeof(status), "%s", app_ctrl_proto_stage_status_text(proto_stage));
    }
    if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy)
    {
        snprintf(status,
            sizeof(status),
            "dock: CH32 err %s",
            app_ch32_link_proto_error_name(proto_error));
    }
    if (retrigger_blocked && !dock_busy && (proto_error == APP_CH32_ERR_NONE) &&
        ((notice == NULL) || (notice[0] == '\0')))
    {
        strlcpy(status, "dock: cooldown / wait next approach", sizeof(status));
    }
    if (app_ctrl_deadline_active(notice_deadline_ms, now_ms) &&
        (notice != NULL) && (notice[0] != '\0'))
    {
        strlcpy(status, notice, sizeof(status));
    }

    app_ui_set_status(status);
    app_ui_set_vision_text(task_brief);
    app_ui_set_dock_text(detail);
    app_ui_update_hud(vision, dock);
}

/* -------------------------------------------------------------------------- */
/* 公开回调和任务入口                                             */
/* -------------------------------------------------------------------------- */

void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx)
{
    (void)user_ctx;

    if (msg == NULL)
    {
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);

    app_ctrl_apply_proto_msg_locked(msg);

    taskEXIT_CRITICAL(&s_ctrl_mux);
}
static void app_ctrl_task(void *arg)
{
    (void)arg;
    bool prev_ready_level = false;
    bool prev_dock_busy = false;

    while (1) {
        const uint32_t now_ms = app_ctrl_now_ms();
        app_vision_result_t vision = {0};
        app_dock_judge_result_t dock = {0};
        app_task_snapshot_t task = {0};
        app_ctrl_loop_state_t state = {0};

        (void)app_vision_get_latest_result(&vision);
        (void)app_dock_judge_process(&vision, &dock);
        (void)app_task_get_snapshot(&task);

        app_ctrl_read_loop_state(&state);
        app_ctrl_apply_task_target_if_needed(&task, state.applied_target_id);
        app_ctrl_probe_ready_if_needed(now_ms, state.ch32_ready, state.last_probe_ms);
        app_ctrl_handle_busy_timeout(now_ms,
            &task,
            &state.dock_busy,
            &state.cargo_wait_window_seen,
            &state.proto_stage,
            &state.proto_error,
            &state.busy_deadline_ms);
        app_ctrl_hold_soft_cargo_wait_if_needed(&state.dock_busy,
            &state.cargo_wait_window_seen,
            &state.proto_stage,
            state.proto_flags,
            &state.proto_error,
            &state.busy_deadline_ms);

        const bool ready_level = (dock.state == APP_DOCK_STATE_READY_TO_DOCK);
        const bool retrigger_blocked = app_ctrl_deadline_active(state.retrigger_deadline_ms, now_ms);

        app_ctrl_mark_auth_if_ready(&task, &dock, ready_level);
        app_ctrl_try_auto_dock(now_ms,
            &dock,
            &task,
            state.ch32_ready,
            retrigger_blocked,
            prev_ready_level,
            ready_level,
            &state.dock_busy,
            &state.cargo_wait_window_seen);
        app_ctrl_update_task_for_busy_transition(prev_dock_busy,
            state.dock_busy,
            state.proto_error,
            &task);

        prev_ready_level = ready_level;
        prev_dock_busy = state.dock_busy;

        app_ctrl_publish_ui(now_ms,
            &vision,
            &dock,
            &task,
            state.ch32_ready,
            state.dock_busy,
            state.has_weight,
            state.weight_g,
            state.proto_stage,
            state.proto_error,
            retrigger_blocked,
            state.notice_deadline_ms,
            state.notice);

        vTaskDelay(pdMS_TO_TICKS(CTRL_POLL_MS));
    }
}
esp_err_t app_ctrl_init(void)
{
    if (s_rt.inited)
    {
        return ESP_OK;
    }
    taskENTER_CRITICAL(&s_ctrl_mux);
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.inited = true;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    ESP_LOGI(TAG, "ctrl init done (auto_dock=%d)", CTRL_AUTO_DOCK_ENABLE);
    return ESP_OK;
}
esp_err_t app_ctrl_start(void)
{
    if (!s_rt.inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctrl_task != NULL)
    {
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(app_ctrl_task,
        "app_ctrl",
        CTRL_TASK_STACK_SIZE,
        NULL,
        CTRL_TASK_PRIORITY,
        &s_ctrl_task,
        CTRL_TASK_CORE_ID);
    if (ret != pdPASS)
    {
        s_ctrl_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ctrl task started");
    return ESP_OK;
}
