#include "app_ctrl_runtime.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_cloud.h"
#include "app_ctrl_proto.h"

// 控制运行时：维护 CH32 机械状态并在每个控制周期推进任务。
//
// "软等货错误"(soft waiting cargo error) 说明：
//   托盘伸出后若出现 TIMEOUT 或 WEIGHT 错误，不是真正故障，
//   而是"货物尚未放上，继续等待"的正常情况。
//   此时保持 dock_busy=true 但取消固定超时，切换到无限等待状态。
//
// 线程模型：
//   - CH32 接收回调（FreeRTOS 任务上下文）写入 s_rt，用 s_ctrl_mux 保护。
//   - app_ctrl_runtime_step() 在控制任务中调用，先拷贝状态快照再处理，
//     最小化临界区持有时间。
static const char *TAG = "app_ctrl";

#define CTRL_READY_PROBE_INTERVAL_MS 1000U
#define CTRL_DOCK_CMD                APP_CH32_PROTO_CMD_START_DOCK
#define CTRL_ACK_WAIT_MS             2000U
#define CTRL_BUSY_TIMEOUT_MS         20000U
#define CTRL_NOTICE_SHOW_MS          1600U
#define CTRL_RETRIGGER_COOLDOWN_MS   1800U

// CH32 状态由 UART 回调写入，由控制任务读取，访问时使用 s_ctrl_mux 保护。
typedef struct {
    bool inited;
    bool ch32_ready;
    bool dock_busy;
    bool cargo_wait_window_seen;
    bool dock_completion_owned_by_start;
    bool manual_retract_pending;
    bool has_weight;
    int32_t last_weight_g;
    app_ch32_proto_cmd_t last_proto_cmd;
    uint16_t last_proto_flags;
    app_ch32_proto_stage_t last_proto_stage;
    uint8_t last_proto_error;
    uint16_t applied_target_id;
    app_task_snapshot_t manual_retract_owner;
    // 以下时间均为毫秒时间点，0 表示当前未启用。
    uint32_t last_ready_probe_ms;
    uint32_t busy_deadline_ms;
    uint32_t notice_deadline_ms;
    uint32_t retrigger_deadline_ms;
    char notice[96];
} app_ctrl_runtime_t;

static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;
static app_ctrl_runtime_t s_rt = {0};

static bool app_ctrl_deadline_active(uint32_t deadline_ms, uint32_t now_ms)
{
    // 用有符号差值比较，正确处理毫秒计时器约 49 天的回绕。
    return deadline_ms != 0U && (int32_t)(deadline_ms - now_ms) > 0;
}

static void app_ctrl_refresh_task(app_ctrl_cycle_t *cycle)
{
    (void)app_task_get_snapshot(&cycle->task);
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

static void app_ctrl_set_notice(const char *text)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_set_notice_locked(text, CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);
}

static void app_ctrl_start_retrigger_cooldown_locked(void)
{
    s_rt.retrigger_deadline_ms = app_ctrl_now_ms() + CTRL_RETRIGGER_COOLDOWN_MS;
}

static bool app_ctrl_task_owner_matches(const app_task_snapshot_t *a,
    const app_task_snapshot_t *b)
{
    return a != NULL && b != NULL &&
           a->generation == b->generation &&
           a->target_id == b->target_id &&
           strcmp(a->source, b->source) == 0;
}

static void app_ctrl_clear_manual_retract_locked(void)
{
    s_rt.manual_retract_pending = false;
    s_rt.dock_completion_owned_by_start = false;
    s_rt.last_proto_cmd = APP_CH32_PROTO_CMD_NONE;
    memset(&s_rt.manual_retract_owner, 0, sizeof(s_rt.manual_retract_owner));
}

// 进入等待放货状态；保持 busy，但取消固定动作超时。
static void app_ctrl_hold_waiting_cargo_locked(void)
{
    s_rt.cargo_wait_window_seen = true;
    s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.dock_busy = true;
    s_rt.busy_deadline_ms = 0;
    app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
}

static void app_ctrl_note_status_owner_locked(const app_ch32_line_t *msg)
{
    const app_ch32_proto_cmd_t cmd = (app_ch32_proto_cmd_t)msg->proto_cmd;
    s_rt.last_proto_cmd = cmd;
    if (cmd == APP_CH32_PROTO_CMD_START_DOCK)
    {
        s_rt.dock_completion_owned_by_start = true;
    }
    else if (cmd != APP_CH32_PROTO_CMD_NONE)
    {
        s_rt.dock_completion_owned_by_start = false;
    }
}

static void app_ctrl_apply_proto_msg_locked(const app_ch32_line_t *msg)
{
    // 保存上一状态，用于判断当前错误是否发生在托盘伸出/等货阶段。
    const app_ch32_proto_stage_t prev_stage = s_rt.last_proto_stage;
    const uint16_t prev_flags = s_rt.last_proto_flags;
    const bool cargo_wait_seen = s_rt.cargo_wait_window_seen;
    const bool was_dock_busy = s_rt.dock_busy;

    s_rt.ch32_ready = app_ch32_link_is_ready();
    if (msg->payload_len >= 8U)
    {
        s_rt.last_weight_g = msg->proto_weight_g;
        s_rt.has_weight = true;
        s_rt.last_proto_flags = msg->proto_flags;
    }
    // ACK/NACK 已由通信层处理，这里只解析机械状态帧。
    if (msg->type != APP_CH32_LINE_PROTO_STATUS)
    {
        return;
    }
    app_ctrl_note_status_owner_locked(msg);

    if (app_ctrl_proto_stage_is_cargo_wait_window(msg->proto_stage) ||
        ((msg->payload_len >= 8U) &&
         app_ctrl_proto_flags_indicate_tray_out(msg->proto_flags)))
    {
        s_rt.cargo_wait_window_seen = true;
    }
    if (s_rt.last_proto_stage != msg->proto_stage)
    {
        s_rt.last_proto_stage = msg->proto_stage;
        app_ctrl_set_notice_locked(app_ctrl_proto_stage_status_text(msg->proto_stage),
            CTRL_NOTICE_SHOW_MS);
    }

    // 托盘已伸出时，TIMEOUT 或 WEIGHT 错误视为”继续等货”而非故障，
    // 调用方将切换到 WAITING_CARGO 无限等待模式，不复位 busy。
    if (msg->proto_detail != APP_CH32_ERR_NONE ||
        msg->proto_stage == APP_CH32_STAGE_FAULT)
    {
        if (app_ctrl_is_soft_waiting_cargo_error(prev_stage,
            prev_flags,
            msg->proto_stage,
            msg->proto_flags,
            msg->proto_detail,
            cargo_wait_seen))
        {
            app_ctrl_hold_waiting_cargo_locked();
            return;
        }

        s_rt.last_proto_error = msg->proto_detail;
        s_rt.dock_busy = false;
        s_rt.dock_completion_owned_by_start = false;
        app_ctrl_clear_manual_retract_locked();
        s_rt.cargo_wait_window_seen = false;
        s_rt.busy_deadline_ms = 0;
        app_ctrl_start_retrigger_cooldown_locked();
        snprintf(s_rt.notice,
            sizeof(s_rt.notice),
            "dock: CH32 err %s",
            app_ch32_link_proto_error_name(msg->proto_detail));
        s_rt.notice_deadline_ms = app_ctrl_now_ms() + CTRL_NOTICE_SHOW_MS;
        return;
    }

    // 阶段或 BUSY 标志任一有效，都认为机构仍在运行。
    if ((msg->proto_flags & APP_CH32_FLAG_BUSY) != 0U ||
        app_ctrl_proto_stage_is_busy(msg->proto_stage))
    {
        s_rt.dock_busy = true;
        s_rt.busy_deadline_ms =
            app_ctrl_proto_stage_uses_busy_deadline(msg->proto_stage) ?
            app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS :
            0;
        return;
    }

    if (msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED ||
        msg->proto_stage == APP_CH32_STAGE_COMPLETE ||
        msg->proto_stage == APP_CH32_STAGE_IDLE ||
        msg->proto_stage == APP_CH32_STAGE_READY)
    {
        s_rt.dock_busy = false;
        s_rt.cargo_wait_window_seen = false;
        s_rt.busy_deadline_ms = 0;
        s_rt.last_proto_error = APP_CH32_ERR_NONE;
        if (was_dock_busy)
        {
            app_ctrl_start_retrigger_cooldown_locked();
        }
    }
}

// 复制当前状态，后续处理不再占用临界区。
static void app_ctrl_read_state(app_ctrl_runtime_t *state)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.ch32_ready = app_ch32_link_is_ready();
    *state = s_rt;
    taskEXIT_CRITICAL(&s_ctrl_mux);
}

// 任务目标变化后，将新的标签号同步给自动对接判定器。
static void app_ctrl_apply_task_target_if_needed(app_ctrl_cycle_t *cycle,
    app_ctrl_runtime_t *state)
{
    if (!cycle->task.target_dirty &&
        state->applied_target_id == cycle->task.target_id)
    {
        return;
    }
    if (app_dock_judge_set_target_id(cycle->task.target_id, true) != ESP_OK)
    {
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.applied_target_id = cycle->task.target_id;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    state->applied_target_id = cycle->task.target_id;
}

static void app_ctrl_probe_ready_if_needed(const app_ctrl_cycle_t *cycle,
    app_ctrl_runtime_t *state)
{
    if (state->ch32_ready ||
        cycle->now_ms - state->last_ready_probe_ms < CTRL_READY_PROBE_INTERVAL_MS)
    {
        return;
    }

    // 每秒最多主动探测一次 CH32。
    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_ready_probe_ms = cycle->now_ms;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    state->last_ready_probe_ms = cycle->now_ms;
    (void)app_ch32_link_probe_ready(200);
    // 探测回复由 UART 回调更新 s_rt；同步刷新本轮快照，
    // 避免用探测前的 false 错过刚完成的鉴权触发。
    state->ch32_ready = app_ch32_link_is_ready();
}

static void app_ctrl_hold_waiting_cargo(app_ctrl_runtime_t *state)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_hold_waiting_cargo_locked();
    taskEXIT_CRITICAL(&s_ctrl_mux);

    state->dock_busy = true;
    state->cargo_wait_window_seen = true;
    state->last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    state->last_proto_error = APP_CH32_ERR_NONE;
    state->busy_deadline_ms = 0;
}

static void app_ctrl_handle_busy_timeout(app_ctrl_cycle_t *cycle,
    app_ctrl_runtime_t *state)
{
    if (!state->dock_busy ||
        state->busy_deadline_ms == 0U ||
        (int32_t)(cycle->now_ms - state->busy_deadline_ms) < 0)
    {
        return;
    }
    // 等货阶段超时表示继续等待货物，其他阶段超时才判为故障。
    if (app_ctrl_proto_stage_is_cargo_wait_window(state->last_proto_stage) ||
        state->cargo_wait_window_seen)
    {
        app_ctrl_hold_waiting_cargo(state);
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.dock_busy = false;
    s_rt.dock_completion_owned_by_start = false;
    app_ctrl_clear_manual_retract_locked();
    s_rt.busy_deadline_ms = 0;
    s_rt.last_proto_error = APP_CH32_ERR_TIMEOUT;
    app_ctrl_start_retrigger_cooldown_locked();
    app_ctrl_set_notice_locked("dock: CH32 timeout", CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);

    state->dock_busy = false;
    state->dock_completion_owned_by_start = false;
    state->busy_deadline_ms = 0;
    state->last_proto_error = APP_CH32_ERR_TIMEOUT;
    if (cycle->task.active || cycle->task.state == APP_TASK_STATE_DOCKING)
    {
        if (app_task_mark_fault_if_current(&cycle->task, "CH32 timeout"))
        {
            app_ctrl_refresh_task(cycle);
        }
    }
}

// 检查当前状态是否符合"软等货"条件（不依赖 prev 状态）。
// 控制循环每轮调用，用于恢复因时序问题丢失的等货状态。
static bool app_ctrl_current_state_is_soft_cargo_wait(const app_ctrl_runtime_t *state)
{
    if (state->dock_busy)
    {
        return false;
    }
    if (!app_ctrl_proto_error_is_cargo_wait_soft(state->last_proto_error))
    {
        return false;
    }
    if (state->last_proto_stage == APP_CH32_STAGE_FAULT ||
        state->last_proto_stage == APP_CH32_STAGE_SAFE_LOCKED ||
        state->last_proto_stage == APP_CH32_STAGE_COMPLETE ||
        (state->last_proto_flags & APP_CH32_FLAG_LOCKED) != 0U)
    {
        return false;
    }
    if (state->cargo_wait_window_seen)
    {
        return app_ctrl_proto_stage_is_cargo_wait_window(state->last_proto_stage) ||
               app_ctrl_proto_flags_indicate_tray_out(state->last_proto_flags);
    }
    return app_ctrl_proto_stage_is_cargo_wait_window(state->last_proto_stage);
}

static void app_ctrl_hold_soft_cargo_wait_if_needed(app_ctrl_runtime_t *state)
{
    if (app_ctrl_current_state_is_soft_cargo_wait(state))
    {
        app_ctrl_hold_waiting_cargo(state);
    }
}

// 成功终态可直接补齐任务完成，防止第二轮接驳时 busy 边沿被快照漏掉。
static bool app_ctrl_state_is_success_terminal(const app_ctrl_runtime_t *state)
{
    if (state == NULL)
    {
        return false;
    }
    if (state->manual_retract_pending)
    {
        if (state->last_proto_stage == APP_CH32_STAGE_SAFE_LOCKED ||
            state->last_proto_stage == APP_CH32_STAGE_COMPLETE)
        {
            return true;
        }
        return (state->last_proto_flags & APP_CH32_FLAG_LIMIT_DOOR_CLOSED) != 0U;
    }
    if (!state->dock_completion_owned_by_start)
    {
        return false;
    }
    switch (state->last_proto_stage) {
    case APP_CH32_STAGE_SAFE_LOCKED:
    case APP_CH32_STAGE_COMPLETE:
        return state->last_proto_cmd == APP_CH32_PROTO_CMD_START_DOCK;
    case APP_CH32_STAGE_IDLE:
    case APP_CH32_STAGE_READY:
        return (state->last_proto_flags & APP_CH32_FLAG_LOCKED) != 0U;
    default:
        return false;
    }
}

// 对接条件满足后，把任务推进到鉴权通过状态。
static void app_ctrl_mark_auth_if_ready(app_ctrl_cycle_t *cycle)
{
    if (!cycle->task.active ||
        cycle->task.state != APP_TASK_STATE_WAIT_APPROACH ||
        !cycle->ready_level)
    {
        return;
    }

    if (app_task_mark_auth_passed_if_current(&cycle->task, cycle->dock.tag_id))
    {
        app_ctrl_refresh_task(cycle);
        app_ctrl_set_notice("auth passed / ready to dock");
    }
}

static void app_ctrl_try_auto_dock(app_ctrl_cycle_t *cycle,
    app_ctrl_runtime_t *state,
    bool retrigger_blocked,
    bool *weather_blocked)
{
    // READY 上升沿触发接驳；已鉴权但发送失败时允许冷却后重试。
    const bool ready_rising = !cycle->prev_ready_level && cycle->ready_level;
    const bool auth_retry =
        cycle->task.state == APP_TASK_STATE_AUTH_PASSED && cycle->ready_level;
    if (!cycle->task.active ||
        state->dock_busy ||
        retrigger_blocked ||
        (!ready_rising && !auth_retry))
    {
        return;
    }

    // 发送机械命令前再次检查天气保护。
    if (app_cloud_is_weather_docking_blocked())
    {
        app_ctrl_set_notice("dock: blocked by weather");
        if (app_task_cancel_if_current(&cycle->task, "blocked by severe weather"))
        {
            app_ctrl_refresh_task(cycle);
        }
        *weather_blocked = true;
        return;
    }
    if (!state->ch32_ready)
    {
        app_ctrl_set_notice("dock: ready but CH32 not ready");
        return;
    }

    ESP_LOGI(TAG,
        "auto dock trigger: tag=%u ready_rising=%u auth_retry=%u",
        (unsigned)cycle->dock.tag_id,
        ready_rising ? 1U : 0U,
        auth_retry ? 1U : 0U);
    esp_err_t ret = app_ch32_link_send_proto_cmd_and_wait_ack(
        CTRL_DOCK_CMD,
        CTRL_ACK_WAIT_MS);
    if (ret == ESP_OK)
    {
        // ACK 只表示命令已接收，完成状态以后续 CH32 状态帧为准。
        taskENTER_CRITICAL(&s_ctrl_mux);
        s_rt.dock_busy = true;
        s_rt.dock_completion_owned_by_start = true;
        s_rt.last_proto_cmd = CTRL_DOCK_CMD;
        s_rt.cargo_wait_window_seen = false;
        s_rt.last_proto_error = APP_CH32_ERR_NONE;
        s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
        s_rt.last_proto_flags = 0;
        s_rt.busy_deadline_ms = cycle->now_ms + CTRL_BUSY_TIMEOUT_MS;
        app_ctrl_start_retrigger_cooldown_locked();
        app_ctrl_set_notice_locked("dock: CH32 accepted start dock",
            CTRL_NOTICE_SHOW_MS);
        taskEXIT_CRITICAL(&s_ctrl_mux);

        state->dock_busy = true;
        state->dock_completion_owned_by_start = true;
        state->last_proto_cmd = CTRL_DOCK_CMD;
        state->cargo_wait_window_seen = false;
        state->last_proto_error = APP_CH32_ERR_NONE;
        if (app_task_mark_docking_started_if_current(&cycle->task))
        {
            app_ctrl_refresh_task(cycle);
        }
        return;
    }

    ESP_LOGW(TAG, "send CH32 cmd START_DOCK failed: %s", esp_err_to_name(ret));
    const bool rejected = ret == ESP_ERR_INVALID_RESPONSE;
    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_proto_error = APP_CH32_ERR_INTERNAL;
    s_rt.dock_completion_owned_by_start = false;
    app_ctrl_start_retrigger_cooldown_locked();
    app_ctrl_set_notice_locked(rejected ?
        "dock: CH32 rejected cmd" :
        "dock: CH32 ack timeout",
        CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);

    state->last_proto_error = APP_CH32_ERR_INTERNAL;
    state->dock_completion_owned_by_start = false;
    if (app_task_mark_fault_if_current(&cycle->task,
        rejected ? "CH32 rejected cmd" : "CH32 ack timeout"))
    {
        app_ctrl_refresh_task(cycle);
    }
}

static void app_ctrl_update_task_for_busy_transition(app_ctrl_cycle_t *cycle,
    const app_ctrl_runtime_t *state)
{
    const bool docking_task =
        cycle->task.active && cycle->task.state == APP_TASK_STATE_DOCKING;
    const bool success_terminal = app_ctrl_state_is_success_terminal(state);
    const bool completion_motion_done =
        state->manual_retract_pending ? success_terminal : !state->dock_busy;

    if (!state->dock_busy && state->last_proto_error != APP_CH32_ERR_NONE)
    {
        if (cycle->task.active || cycle->task.state == APP_TASK_STATE_DOCKING)
        {
            if (app_task_mark_fault_if_current(&cycle->task,
                app_ch32_link_proto_error_name(state->last_proto_error)))
            {
                app_ctrl_refresh_task(cycle);
            }
        }
        return;
    }

    // 根据机构从空闲到忙碌、再回到空闲的过程推进任务状态。
    if (!cycle->prev_dock_busy &&
        state->dock_busy &&
        cycle->task.active &&
        cycle->task.state != APP_TASK_STATE_DOCKING)
    {
        if (app_task_mark_docking_started_if_current(&cycle->task))
        {
            app_ctrl_refresh_task(cycle);
        }
        return;
    }

    if (completion_motion_done &&
        success_terminal &&
        (cycle->prev_dock_busy || docking_task) &&
        (cycle->task.active || cycle->task.state == APP_TASK_STATE_DOCKING))
    {
        if (!cycle->prev_dock_busy)
        {
            ESP_LOGW(TAG,
                "recover dock completion from terminal cmd=0x%02x stage=%s flags=0x%04x",
                (unsigned)state->last_proto_cmd,
                app_ch32_link_proto_stage_name(state->last_proto_stage),
                (unsigned)state->last_proto_flags);
        }
        const bool manual_retract = state->manual_retract_pending;
        const app_task_snapshot_t *owner = manual_retract ?
            &state->manual_retract_owner : &cycle->task;
        const char *note = manual_retract ?
            "manual retract fallback completed" : "dock cycle done";
        if (app_task_mark_completed_if_current(owner, note))
        {
            app_ctrl_refresh_task(cycle);
        }
        if (manual_retract)
        {
            app_ctrl_runtime_cancel_manual_retract(owner);
        }
    }
}

// 整理 UI 需要显示的机械状态和临时提示。
static void app_ctrl_project_runtime_view(app_ctrl_cycle_t *cycle,
    const app_ctrl_runtime_t *state,
    bool weather_blocked)
{
    app_ctrl_runtime_view_t *view = &cycle->runtime;
    memset(view, 0, sizeof(*view));
    view->ch32_ready = state->ch32_ready;
    view->dock_busy = state->dock_busy;
    view->has_weight = state->has_weight;
    view->weight_g = state->last_weight_g;
    view->proto_stage = state->last_proto_stage;
    view->proto_error = state->last_proto_error;
    view->retrigger_blocked =
        app_ctrl_deadline_active(state->retrigger_deadline_ms, cycle->now_ms);
    view->notice_active =
        app_ctrl_deadline_active(state->notice_deadline_ms, cycle->now_ms);
    view->weather_blocked = weather_blocked;
    strlcpy(view->notice, state->notice, sizeof(view->notice));
}

esp_err_t app_ctrl_runtime_init(void)
{
    if (app_ctrl_runtime_is_initialized())
    {
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.last_proto_cmd = APP_CH32_PROTO_CMD_NONE;
    s_rt.dock_completion_owned_by_start = false;
    s_rt.inited = true;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    return ESP_OK;
}

bool app_ctrl_runtime_is_initialized(void)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    const bool inited = s_rt.inited;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    return inited;
}

bool app_ctrl_runtime_begin_manual_retract(const app_task_snapshot_t *owner)
{
    if (owner == NULL || !owner->active || owner->generation == 0U)
    {
        return false;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    if (!s_rt.inited)
    {
        taskEXIT_CRITICAL(&s_ctrl_mux);
        return false;
    }
    s_rt.manual_retract_pending = true;
    s_rt.manual_retract_owner = *owner;
    s_rt.dock_busy = true;
    s_rt.cargo_wait_window_seen = false;
    s_rt.dock_completion_owned_by_start = false;
    s_rt.last_proto_cmd = APP_CH32_PROTO_CMD_SAFE_CLOSE;
    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
    s_rt.last_proto_flags = 0U;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.busy_deadline_ms = app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS;
    app_ctrl_set_notice_locked("dock: manual fallback retract", CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);
    return true;
}

void app_ctrl_runtime_cancel_manual_retract(const app_task_snapshot_t *owner)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    if (s_rt.manual_retract_pending &&
        app_ctrl_task_owner_matches(&s_rt.manual_retract_owner, owner))
    {
        app_ctrl_clear_manual_retract_locked();
    }
    taskEXIT_CRITICAL(&s_ctrl_mux);
}

// CH32 接收回调只更新状态，不执行发送命令等耗时操作。
void app_ctrl_runtime_on_ch32_line(const app_ch32_line_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_apply_proto_msg_locked(msg);
    taskEXIT_CRITICAL(&s_ctrl_mux);
}

void app_ctrl_runtime_step(app_ctrl_cycle_t *cycle)
{
    if (cycle == NULL)
    {
        return;
    }

    app_ctrl_runtime_t state = {0};
    bool weather_blocked = false;
    app_ctrl_read_state(&state);

    // 先处理已有状态和超时，再判断是否触发新的接驳命令，
    // 确保本轮读到的状态已经过期/超时处理后再做决策。
    app_ctrl_apply_task_target_if_needed(cycle, &state);
    app_ctrl_probe_ready_if_needed(cycle, &state);
    app_ctrl_handle_busy_timeout(cycle, &state);
    app_ctrl_hold_soft_cargo_wait_if_needed(&state);

    const bool retrigger_blocked =
        app_ctrl_deadline_active(state.retrigger_deadline_ms, cycle->now_ms);
    app_ctrl_mark_auth_if_ready(cycle);
    app_ctrl_try_auto_dock(cycle, &state, retrigger_blocked, &weather_blocked);
    app_ctrl_update_task_for_busy_transition(cycle, &state);

    // 发送命令期间可能收到新状态，结束前重新读取一次。
    app_ctrl_read_state(&state);
    app_ctrl_project_runtime_view(cycle, &state, weather_blocked);
}
