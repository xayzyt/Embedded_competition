#include "app_ctrl_runtime.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_cloud.h"
#include "app_ctrl_proto.h"

static const char *TAG = "app_ctrl";

#define CTRL_READY_PROBE_INTERVAL_MS 1000U
#define CTRL_DOCK_CMD                APP_CH32_PROTO_CMD_START_DOCK
#define CTRL_ACK_WAIT_MS             2000U
#define CTRL_BUSY_TIMEOUT_MS         20000U
#define CTRL_NOTICE_SHOW_MS          1600U
#define CTRL_RETRIGGER_COOLDOWN_MS   1800U

typedef struct {
    bool inited;
    bool ch32_ready;
    bool dock_busy;
    bool cargo_wait_window_seen;
    bool has_weight;
    int32_t last_weight_g;
    uint16_t last_proto_flags;
    app_ch32_proto_stage_t last_proto_stage;
    uint8_t last_proto_error;
    uint16_t applied_target_id;
    uint32_t last_ready_probe_ms;
    uint32_t busy_deadline_ms;
    uint32_t notice_deadline_ms;
    uint32_t retrigger_deadline_ms;
    char notice[96];
} app_ctrl_runtime_t;

static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;
static app_ctrl_runtime_t s_rt = {0};

static uint32_t app_ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool app_ctrl_deadline_active(uint32_t deadline_ms, uint32_t now_ms)
{
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

static void app_ctrl_hold_waiting_cargo_locked(void)
{
    s_rt.cargo_wait_window_seen = true;
    s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.dock_busy = true;
    s_rt.busy_deadline_ms = 0;
    app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
}

static void app_ctrl_apply_proto_msg_locked(const app_ch32_line_t *msg)
{
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
    if (msg->type != APP_CH32_LINE_PROTO_STATUS)
    {
        return;
    }

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

static void app_ctrl_read_state(app_ctrl_runtime_t *state)
{
    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.ch32_ready = app_ch32_link_is_ready();
    *state = s_rt;
    taskEXIT_CRITICAL(&s_ctrl_mux);
}

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

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_ready_probe_ms = cycle->now_ms;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    state->last_ready_probe_ms = cycle->now_ms;
    (void)app_ch32_link_probe_ready(200);
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
    if (app_ctrl_proto_stage_is_cargo_wait_window(state->last_proto_stage) ||
        state->cargo_wait_window_seen)
    {
        app_ctrl_hold_waiting_cargo(state);
        return;
    }

    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.dock_busy = false;
    s_rt.busy_deadline_ms = 0;
    s_rt.last_proto_error = APP_CH32_ERR_TIMEOUT;
    app_ctrl_start_retrigger_cooldown_locked();
    app_ctrl_set_notice_locked("dock: CH32 timeout", CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);

    state->dock_busy = false;
    state->busy_deadline_ms = 0;
    state->last_proto_error = APP_CH32_ERR_TIMEOUT;
    if (cycle->task.active || cycle->task.state == APP_TASK_STATE_DOCKING)
    {
        app_task_mark_fault("CH32 timeout");
        app_ctrl_refresh_task(cycle);
    }
}

static void app_ctrl_hold_soft_cargo_wait_if_needed(app_ctrl_runtime_t *state)
{
    if (!state->dock_busy &&
        app_ctrl_is_soft_waiting_cargo_error(state->last_proto_stage,
            state->last_proto_flags,
            state->last_proto_stage,
            state->last_proto_flags,
            state->last_proto_error,
            state->cargo_wait_window_seen))
    {
        app_ctrl_hold_waiting_cargo(state);
    }
}

static void app_ctrl_mark_auth_if_ready(app_ctrl_cycle_t *cycle)
{
    if (!cycle->task.active ||
        cycle->task.state != APP_TASK_STATE_WAIT_APPROACH ||
        !cycle->ready_level)
    {
        return;
    }

    app_task_mark_auth_passed(cycle->dock.tag_id);
    app_ctrl_refresh_task(cycle);
    app_ctrl_set_notice("auth passed / ready to dock");
}

static void app_ctrl_try_auto_dock(app_ctrl_cycle_t *cycle,
    app_ctrl_runtime_t *state,
    bool retrigger_blocked,
    bool *weather_blocked)
{
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

    if (app_cloud_is_weather_docking_blocked())
    {
        app_ctrl_set_notice("dock: blocked by weather");
        app_task_cancel("blocked by severe weather");
        app_ctrl_refresh_task(cycle);
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
        taskENTER_CRITICAL(&s_ctrl_mux);
        s_rt.dock_busy = true;
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
        state->cargo_wait_window_seen = false;
        state->last_proto_error = APP_CH32_ERR_NONE;
        app_task_mark_docking_started();
        app_ctrl_refresh_task(cycle);
        return;
    }

    ESP_LOGW(TAG, "send CH32 cmd START_DOCK failed: %s", esp_err_to_name(ret));
    const bool rejected = ret == ESP_ERR_INVALID_RESPONSE;
    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_proto_error = APP_CH32_ERR_INTERNAL;
    app_ctrl_start_retrigger_cooldown_locked();
    app_ctrl_set_notice_locked(rejected ?
        "dock: CH32 rejected cmd" :
        "dock: CH32 ack timeout",
        CTRL_NOTICE_SHOW_MS);
    taskEXIT_CRITICAL(&s_ctrl_mux);

    state->last_proto_error = APP_CH32_ERR_INTERNAL;
    app_task_mark_fault(rejected ? "CH32 rejected cmd" : "CH32 ack timeout");
    app_ctrl_refresh_task(cycle);
}

static void app_ctrl_update_task_for_busy_transition(app_ctrl_cycle_t *cycle,
    const app_ctrl_runtime_t *state)
{
    if (!state->dock_busy && state->last_proto_error != APP_CH32_ERR_NONE)
    {
        if (cycle->task.active || cycle->task.state == APP_TASK_STATE_DOCKING)
        {
            app_task_mark_fault(
                app_ch32_link_proto_error_name(state->last_proto_error));
            app_ctrl_refresh_task(cycle);
        }
        return;
    }

    if (!cycle->prev_dock_busy &&
        state->dock_busy &&
        cycle->task.active &&
        cycle->task.state != APP_TASK_STATE_DOCKING)
    {
        app_task_mark_docking_started();
        app_ctrl_refresh_task(cycle);
        return;
    }

    if (cycle->prev_dock_busy &&
        !state->dock_busy &&
        (cycle->task.active || cycle->task.state == APP_TASK_STATE_DOCKING))
    {
        app_task_mark_completed("dock cycle done");
        app_ctrl_refresh_task(cycle);
    }
}

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

    app_ctrl_apply_task_target_if_needed(cycle, &state);
    app_ctrl_probe_ready_if_needed(cycle, &state);
    app_ctrl_handle_busy_timeout(cycle, &state);
    app_ctrl_hold_soft_cargo_wait_if_needed(&state);

    const bool retrigger_blocked =
        app_ctrl_deadline_active(state.retrigger_deadline_ms, cycle->now_ms);
    app_ctrl_mark_auth_if_ready(cycle);
    app_ctrl_try_auto_dock(cycle, &state, retrigger_blocked, &weather_blocked);
    app_ctrl_update_task_for_busy_transition(cycle, &state);

    // Include notices and UART updates that arrived while this cycle was running.
    app_ctrl_read_state(&state);
    app_ctrl_project_runtime_view(cycle, &state, weather_blocked);
}
