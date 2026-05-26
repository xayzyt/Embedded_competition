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
#include "app_cloud.h"
#include "app_ctrl_proto.h"
#include "app_ctrl_text.h"
#include "app_dock_judge.h"
#include "app_drone_ai.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"
static const char *TAG = "app_ctrl";
#define CTRL_TASK_STACK_SIZE            (7 * 1024)
#define CTRL_TASK_PRIORITY              5
#define CTRL_TASK_CORE_ID               1
#define CTRL_POLL_MS                    60U
#define CTRL_READY_PROBE_INTERVAL_MS    1000U
#define CTRL_DOCK_CMD                   APP_CH32_PROTO_CMD_START_DOCK
#define CTRL_ACK_WAIT_MS                2000U
#define CTRL_BUSY_TIMEOUT_MS            20000U
#define CTRL_NOTICE_SHOW_MS             1600U
#define CTRL_RETRIGGER_COOLDOWN_MS      1800U
#define CTRL_AUTO_DOCK_ENABLE           (1)
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
typedef struct {
    bool ch32_ready;
    bool dock_busy;
    bool cargo_wait_window_seen;
    bool has_weight;
    int32_t weight_g;
    uint16_t proto_flags;
    app_ch32_proto_stage_t proto_stage;
    uint8_t proto_error;
    uint16_t applied_target_id;
    uint32_t last_probe_ms;
    uint32_t busy_deadline_ms;
    uint32_t notice_deadline_ms;
    uint32_t retrigger_deadline_ms;
    char notice[96];
} app_ctrl_loop_state_t;
static TaskHandle_t s_ctrl_task = NULL;
static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;
static app_ctrl_runtime_t s_rt = {0};
static void app_ctrl_on_task_event(app_task_event_t event,
    const app_task_snapshot_t *snap,
    void *user_ctx)
{
    (void)user_ctx;
    if (event != APP_TASK_EVENT_STATE_CHANGED || snap == NULL)
    {
        return;
    }
    switch (snap->state) {
    case APP_TASK_STATE_WAIT_APPROACH:
    case APP_TASK_STATE_COMPLETED:
    case APP_TASK_STATE_FAULT:
    case APP_TASK_STATE_CANCELLED:
        app_drone_ai_reset_gate();
        break;
    default:
        break;
    }
}
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
    const bool ready_rising = !prev_ready_level && ready_level;
    const bool auth_ready_retry = (task->state == APP_TASK_STATE_AUTH_PASSED) && ready_level;
    if (!(task->active && !*dock_busy && !retrigger_blocked && (ready_rising || auth_ready_retry)))
    {
        return;
    }
    if (app_cloud_is_weather_docking_blocked())
    {
        app_ctrl_set_notice("dock: blocked by weather", CTRL_NOTICE_SHOW_MS);
        app_task_cancel("blocked by severe weather");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
        (void)app_task_get_snapshot(task);
        return;
    }
    if (!ch32_ready)
    {
        app_ctrl_set_notice("dock: ready but CH32 not ready", CTRL_NOTICE_SHOW_MS);
        return;
    }
    ESP_LOGI(TAG,
        "READY rising edge -> send CH32 cmd START_DOCK (id=%u dx=%ld dy=%ld z=%ld score=%u)",
        (unsigned)dock->tag_id,
        (long)dock->dx,
        (long)dock->dy,
        (long)dock->est_distance_mm,
        (unsigned)dock->hover_score);
    esp_err_t ret = app_ch32_link_send_proto_cmd_and_wait_ack(CTRL_DOCK_CMD, CTRL_ACK_WAIT_MS);
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
    ESP_LOGW(TAG, "send CH32 cmd START_DOCK failed: %s", esp_err_to_name(ret));
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
    bool prev_apriltag_enabled = false;
    uint32_t gate_open_vision_seq = 0;
    while (1) {
        const uint32_t now_ms = app_ctrl_now_ms();
        app_vision_result_t vision = {0};
        app_dock_judge_result_t dock = {0};
        app_task_snapshot_t task = {0};
        app_ctrl_loop_state_t state = {0};
        (void)app_vision_get_latest_result(&vision);
        (void)app_dock_judge_process(&vision, &dock);
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
        prev_apriltag_enabled = apriltag_enabled;
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
    esp_err_t cb_ret = app_task_register_event_callback(app_ctrl_on_task_event, NULL);
    if (cb_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "register ctrl task callback failed: %s", esp_err_to_name(cb_ret));
    }
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
