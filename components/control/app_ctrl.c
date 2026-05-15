/* 实现说明：通过超时点和冷却窗口避免反复触发 CH32 执行动作。 */
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
#include "app_ctrl_proto.h"
#include "app_ctrl_text.h"
#include "app_dock_judge.h"
#include "app_drone_ai.h"
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

/* 获取当前系统毫秒时间，供控制循环和超时判断使用。 */
static inline uint32_t app_ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/* 判断某个截止时间在当前时刻是否仍然有效。 */
static inline bool app_ctrl_deadline_active(uint32_t deadline_ms, uint32_t now_ms)
{
    return (deadline_ms != 0U) && ((int32_t)(deadline_ms - now_ms) > 0);
}
/* 在已持锁状态下设置临时 UI 提示及其保留时间。 */
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
/* 对外层辅助逻辑设置临时 UI 提示，内部负责加锁。 */
static void app_ctrl_set_notice(const char *text, uint32_t hold_ms)
{

    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_set_notice_locked(text, hold_ms);

    taskEXIT_CRITICAL(&s_ctrl_mux);
}
/* 在已持锁状态下启动防重复触发冷却窗口。 */
static void app_ctrl_start_retrigger_cooldown_locked(uint32_t hold_ms)
{
    s_rt.retrigger_deadline_ms = app_ctrl_now_ms() + hold_ms;
}

/* -------------------------------------------------------------------------- */
/* CH32 协议状态辅助函数                                                 */
/* -------------------------------------------------------------------------- */

/* 在已持锁状态下保持等待放货状态，避免误判为接驳失败。 */
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

/* 在已持锁状态下消化 CH32 状态帧并更新控制运行态。 */
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
    if (msg->type == APP_CH32_LINE_PROTO_STATUS)
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
        if ((msg->proto_detail != APP_CH32_ERR_NONE) || (msg->proto_stage == APP_CH32_STAGE_FAULT))
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

/* 拷贝一份控制运行态快照，供本轮控制循环在锁外使用。 */
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

/* 当任务目标 ID 变化时，同步到接驳判定模块。 */
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

/* CH32 未 ready 时按固定间隔主动探测链路状态。 */
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

/* 同步更新全局状态和本轮局部状态为等待放货。 */
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

/* 处理接驳 busy 阶段超时，并决定是等待放货还是进入故障。 */
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

/* 在软错误场景下把本轮状态保持为等待放货。 */
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

/* 当视觉判定首次达到 ready 时，将任务推进到认证通过。 */
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

/* 在 ready 上升沿且 CH32 可用时自动下发接驳启动命令。 */
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
    const bool ready_rising = !prev_ready_level && ready_level;
    const bool auth_ready_retry = (task->state == APP_TASK_STATE_AUTH_PASSED) && ready_level;
    if (!(task->active && !*dock_busy && !retrigger_blocked && (ready_rising || auth_ready_retry)))
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

/* 根据 dock_busy 的边沿变化同步任务的 docking、完成或故障状态。 */
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

/* 汇总任务、视觉、CH32 和提示信息并刷新 UI 文本/HUD。 */
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
    bool apriltag_enabled,
    uint32_t notice_deadline_ms,
    const char *notice)
{
    char status[128] = {0};
    char detail[224] = {0};
    char task_brief[96] = {0};

    app_task_format_brief(task, task_brief, sizeof(task_brief));
    app_ctrl_compose_task_status(task, dock, ch32_ready, apriltag_enabled, status, sizeof(status));
    app_ctrl_compose_detail(dock, has_weight, weight_g, proto_stage, detail, sizeof(detail));
    if (task != NULL && task->active && task->state == APP_TASK_STATE_WAIT_APPROACH && !apriltag_enabled)
    {
        char ai_status[64] = {0};
        app_drone_ai_format_status(ai_status, sizeof(ai_status));
        snprintf(detail, sizeof(detail), "dock dbg: apriltag gated / %s", ai_status);
    }

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

    app_ui_update_control_state(status, task_brief, detail, vision, dock);
}

/* -------------------------------------------------------------------------- */
/* 公开回调和任务入口                                             */
/* -------------------------------------------------------------------------- */

/* 作为 CH32 链路回调入口，收到一帧解析结果后更新控制状态。 */
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
/* 控制循环主任务，周期性串起视觉判定、任务推进、CH32 命令和 UI 刷新。 */
static void app_ctrl_task(void *arg)
{
    (void)arg;
    bool prev_ready_level = false;
    bool prev_dock_busy = false;
    bool prev_apriltag_enabled = false;
    uint32_t gate_open_vision_seq = 0;

    while (1) {
        /* 获取当前系统毫秒时间戳，供本轮所有超时判断使用。 */
        const uint32_t now_ms = app_ctrl_now_ms();

        app_vision_result_t vision = {0};
        app_dock_judge_result_t dock = {0};
        app_task_snapshot_t task = {0};
        app_ctrl_loop_state_t state = {0};

        /* 感知层：读最新视觉检测结果。 */
        (void)app_vision_get_latest_result(&vision);
        /* 判定层：将视觉结果输入接驳判定，输出对准状态。 */
        (void)app_dock_judge_process(&vision, &dock);
        /* 读当前任务快照（状态、目标 ID 等）。 */
        (void)app_task_get_snapshot(&task);
        const bool apriltag_enabled = task.active &&
                                      (task.state == APP_TASK_STATE_WAIT_APPROACH) &&
                                      app_drone_ai_is_drone_confirmed();
        if (!apriltag_enabled)
        {
            gate_open_vision_seq = vision.frame_seq;
            memset(&vision, 0, sizeof(vision));
            app_dock_judge_reset();
            (void)app_dock_judge_process(&vision, &dock);
            prev_ready_level = false;
        }
        else if (!prev_apriltag_enabled)
        {
            gate_open_vision_seq = vision.frame_seq;
            memset(&vision, 0, sizeof(vision));
            app_dock_judge_reset();
            (void)app_dock_judge_process(&vision, &dock);
            prev_ready_level = false;
        }
        else if (vision.frame_seq <= gate_open_vision_seq)
        {
            memset(&vision, 0, sizeof(vision));
            app_dock_judge_reset();
            (void)app_dock_judge_process(&vision, &dock);
            prev_ready_level = false;
        }

        /* 将 CH32 回调更新的共享状态拷贝到本轮局部变量，后续判断在锁外进行。 */
        app_ctrl_read_loop_state(&state);
        /* 如果任务目标 ID 发生变化，同步给接驳判定模块。 */
        app_ctrl_apply_task_target_if_needed(&task, state.applied_target_id);
        /* CH32 未 ready 时按固定间隔主动探测链路状态。 */
        app_ctrl_probe_ready_if_needed(now_ms, state.ch32_ready, state.last_probe_ms);
        /* 处理接驳 busy 阶段超时，超时后按情况进入等待放货或故障。 */
        app_ctrl_handle_busy_timeout(now_ms,
            &task,
            &state.dock_busy,
            &state.cargo_wait_window_seen,
            &state.proto_stage,
            &state.proto_error,
            &state.busy_deadline_ms);
        /* 在软错误场景下（如等待放货时的超时/重量异常）保持等待放货状态，避免误判故障。 */
        app_ctrl_hold_soft_cargo_wait_if_needed(&state.dock_busy,
            &state.cargo_wait_window_seen,
            &state.proto_stage,
            state.proto_flags,
            &state.proto_error,
            &state.busy_deadline_ms);

        /* 当前是否达到"可以接驳"的判定等级。 */
        const bool ready_level = (dock.state == APP_DOCK_STATE_READY_TO_DOCK);
        /* 防重复触发冷却窗口是否仍在有效期内。 */
        const bool retrigger_blocked = app_ctrl_deadline_active(state.retrigger_deadline_ms, now_ms);

        /* 视觉首次达到 ready 时，将任务推进到认证通过。 */
        app_ctrl_mark_auth_if_ready(&task, &dock, ready_level);
        /* ready 上升沿 + CH32 可用时，自动下发 START_DOCK 命令。 */
        app_ctrl_try_auto_dock(now_ms,
            &dock,
            &task,
            state.ch32_ready,
            retrigger_blocked,
            prev_ready_level,
            ready_level,
            &state.dock_busy,
            &state.cargo_wait_window_seen);
        /* 根据 dock_busy 的边沿变化同步任务的 docking、完成或故障状态。 */
        app_ctrl_update_task_for_busy_transition(prev_dock_busy,
            state.dock_busy,
            state.proto_error,
            &task);

        /* 保存本轮状态供下一轮做边沿检测。 */
        prev_ready_level = ready_level;
        prev_dock_busy = state.dock_busy;
        prev_apriltag_enabled = apriltag_enabled;

        /* 汇总视觉、CH32、任务状态，刷新 UI 文本和 HUD。 */
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
            apriltag_enabled,
            state.notice_deadline_ms,
            state.notice);

        /* 控制循环休眠 60ms 后进入下一轮。 */
        vTaskDelay(pdMS_TO_TICKS(CTRL_POLL_MS));
    }
}
/* 初始化控制模块的共享运行状态。 */
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
/* 创建控制循环 FreeRTOS 任务，重复调用会直接返回成功。 */
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
