#include "app_ctrl.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "bsp/esp-bsp.h"
#include "bsp_display_port.h"
static const char *TAG = "app_ctrl";
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
static TaskHandle_t s_ctrl_task = NULL;
static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;
static app_ctrl_runtime_t s_rt = {0};
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
    if (text == NULL) {
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
static bool app_ctrl_line_has_done_keyword(const char *line)
{
    if (line == NULL) {
        return false;
    }
    return (strstr(line, "FLOW_DONE") != NULL) ||
           (strstr(line, "FULLFLOW_DONE") != NULL) ||
           (strstr(line, "ALL_DONE") != NULL) ||
           (strstr(line, "CYCLE_DONE") != NULL) ||
           (strstr(line, "COMPLETE") != NULL) ||
           (strstr(line, "SAFE_LOCKED") != NULL) ||
           (strstr(line, "FLOW_OK") != NULL) ||
           (strstr(line, "IDLE") != NULL);
}
static bool app_ctrl_line_indicates_cargo_wait_window(const char *line)
{
    if (line == NULL) {
        return false;
    }
    return (strstr(line, "TRAY_EXTENDED") != NULL) ||
           (strstr(line, "WAITING_CARGO") != NULL) ||
           (strstr(line, "tray extended") != NULL) ||
           (strstr(line, "waiting cargo") != NULL) ||
           (strstr(line, "drawer has been fully extended") != NULL) ||
           (strstr(line, "tray has been fully extended") != NULL);
}
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
    if (!app_ctrl_proto_error_is_cargo_wait_soft(proto_error)) {
        return false;
    }
    if (cargo_wait_window_seen) {
        return true;
    }
    if (app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage)) {
        return true;
    }
    if ((stage == APP_CH32_STAGE_FAULT) &&
        (app_ctrl_proto_stage_is_cargo_wait_window(prev_stage) || tray_out_now || tray_out_before)) {
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
static void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
                                    bool has_weight,
                                    int32_t weight_g,
                                    app_ch32_proto_stage_t proto_stage,
                                    char *buf,
                                    size_t buf_len)
{
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U)) {
        return;
    }
    if (!dock->vision_valid) {
        if (dock->state != APP_DOCK_STATE_SEARCHING) {
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
        } else {
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        }
        return;
    }
    snprintf(buf,
             buf_len,
             "dock dbg: id:%u dx:%ld dy:%ld z:%ldmm e:%.1f ang:%d st:%u score:%u wt:%s%ldg",
             (unsigned)dock->tag_id,
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
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U)) {
        return;
    }
    if (!dock->vision_valid) {
        snprintf(buf, buf_len, "dock: searching target");
        return;
    }
    if (!dock->target_id_ok) {
        snprintf(buf, buf_len, "dock: wrong tag id");
        return;
    }
    if (!dock->centered_ok) {
        snprintf(buf, buf_len, "dock: align target center");
        return;
    }
    if (!dock->near_ok) {
        snprintf(buf, buf_len, "dock: move target closer");
        return;
    }
    if (!dock->stable_ok) {
        snprintf(buf, buf_len, "dock: hold hover stable");
        return;
    }
    if (!dock->distance_ok) {
        if (dock->est_distance_mm > 0) {
            snprintf(buf,
                     buf_len,
                     (dock->est_distance_mm < 260) ? "dock: target too near" : "dock: target too far");
        } else {
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
    if (buf == NULL || buf_len == 0U || task == NULL) {
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
    if (msg == NULL) {
        return;
    }
    const app_ch32_proto_stage_t prev_proto_stage = s_rt.last_proto_stage;
    const uint16_t prev_proto_flags = s_rt.last_proto_flags;
    const bool prev_cargo_wait_window_seen = s_rt.cargo_wait_window_seen;
    s_rt.ch32_ready = app_ch32_link_is_ready();
    if (msg->payload_len >= 8U) {
        s_rt.last_weight_g = msg->proto_weight_g;
        s_rt.has_weight = true;
        s_rt.last_proto_flags = msg->proto_flags;
    }
    if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
        (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
        (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
        (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        if (msg->payload_len >= 8U) {
            s_rt.last_proto_flags = msg->proto_flags;
        }
        if (app_ctrl_proto_stage_is_cargo_wait_window(msg->proto_stage) ||
            ((msg->payload_len >= 8U) && app_ctrl_proto_flags_indicate_tray_out(msg->proto_flags))) {
            s_rt.cargo_wait_window_seen = true;
        }
        if (s_rt.last_proto_stage != msg->proto_stage) {
            s_rt.last_proto_stage = msg->proto_stage;
            app_ctrl_set_notice_locked(app_ctrl_proto_stage_status_text(msg->proto_stage), CTRL_NOTICE_SHOW_MS);
        }
        if ((msg->type == APP_CH32_LINE_PROTO_ERROR) || (msg->proto_stage == APP_CH32_STAGE_FAULT)) {
            if (app_ctrl_is_soft_waiting_cargo_error(prev_proto_stage,
                                                     prev_proto_flags,
                                                     msg->proto_stage,
                                                     msg->proto_flags,
                                                     msg->proto_detail,
                                                     prev_cargo_wait_window_seen)) {
                app_ctrl_hold_waiting_cargo_locked();
                return;
            }
            s_rt.last_proto_error = msg->proto_detail;
            s_rt.dock_busy = false;
            s_rt.busy_deadline_ms = 0;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
            snprintf(s_rt.notice,
                     sizeof(s_rt.notice),
                     "dock: CH32 err %s",
                     app_ch32_link_proto_error_name(msg->proto_detail));
            s_rt.notice_deadline_ms = app_ctrl_now_ms() + CTRL_NOTICE_SHOW_MS;
            return;
        }
        if (((msg->proto_flags & APP_CH32_FLAG_BUSY) != 0U) || app_ctrl_proto_stage_is_busy(msg->proto_stage)) {
            s_rt.dock_busy = true;
            if (app_ctrl_proto_stage_uses_busy_deadline(msg->proto_stage)) {
                s_rt.busy_deadline_ms = app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS;
            } else {
                s_rt.busy_deadline_ms = 0;
            }
            return;
        }
        if ((msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED) ||
            (msg->proto_stage == APP_CH32_STAGE_COMPLETE) ||
            (msg->proto_stage == APP_CH32_STAGE_IDLE) ||
            (msg->proto_stage == APP_CH32_STAGE_READY)) {
            s_rt.dock_busy = false;
            s_rt.busy_deadline_ms = 0;
            s_rt.last_proto_error = APP_CH32_ERR_NONE;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
        }
    }
}
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx)
{
    (void)user_ctx;
    if (msg == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_ctrl_mux);
    if (msg->is_proto) {
        app_ctrl_apply_proto_msg_locked(msg);
        taskEXIT_CRITICAL(&s_ctrl_mux);
        return;
    }
    if (msg->type == APP_CH32_LINE_STATUS) {
        if (strstr(msg->line, "CH32_READY") != NULL) {
            s_rt.ch32_ready = true;
            app_ctrl_set_notice_locked("dock: CH32 ready", CTRL_NOTICE_SHOW_MS);
        }
        const char *w = strstr(msg->line, "WEIGHT=");
        if (w != NULL) {
            s_rt.last_weight_g = (int32_t)strtol(w + 7, NULL, 10);
            s_rt.has_weight = true;
        }
        if (app_ctrl_line_indicates_cargo_wait_window(msg->line)) {
            s_rt.cargo_wait_window_seen = true;
            s_rt.dock_busy = true;
            s_rt.busy_deadline_ms = 0;
            s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
            app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
        }
        if (app_ctrl_line_has_done_keyword(msg->line)) {
            s_rt.dock_busy = false;
            s_rt.cargo_wait_window_seen = false;
            s_rt.busy_deadline_ms = 0;
            s_rt.last_proto_error = APP_CH32_ERR_NONE;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
            app_ctrl_set_notice_locked("dock: CH32 cycle done", CTRL_NOTICE_SHOW_MS);
        }
    } else if (msg->type == APP_CH32_LINE_ERROR) {
        s_rt.dock_busy = false;
        s_rt.busy_deadline_ms = 0;
        app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
        app_ctrl_set_notice_locked("dock: CH32 error", CTRL_NOTICE_SHOW_MS);
    }
    taskEXIT_CRITICAL(&s_ctrl_mux);
}
static void app_ctrl_handle_touch(const app_task_snapshot_t *task,
                                  bool dock_busy,
                                  uint32_t now_ms)
{
    (void)task;
    (void)dock_busy;
    (void)now_ms;
    return;
#if 0
    int32_t x = 0;
    int32_t y = 0;
    if (!app_display_touch_read(&x, &y)) {
        return;
    }
    app_ui_set_coord(x, y);
    taskENTER_CRITICAL(&s_ctrl_mux);
    const uint32_t last_touch_ms = s_rt.last_touch_ms;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    if (now_ms - last_touch_ms < CTRL_TOUCH_DEBOUNCE_MS) {
        return;
    }
    taskENTER_CRITICAL(&s_ctrl_mux);
    s_rt.last_touch_ms = now_ms;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    const int32_t w = BSP_LCD_H_RES;
    const int32_t h = BSP_LCD_V_RES;
    const bool top_half = (y < (h / 2));
    const bool left_half = (x < (w / 2));
    if (top_half) {
        if (dock_busy || (task->state == APP_TASK_STATE_DOCKING)) {
            app_ctrl_set_notice("cfg locked while docking", CTRL_NOTICE_SHOW_MS);
            return;
        }
        uint16_t target = task->target_id;
        if (left_half) {
            target = (target > CTRL_TASK_ID_MIN) ? (uint16_t)(target - 1U) : CTRL_TASK_ID_MIN;
        } else {
            target = (target < CTRL_TASK_ID_MAX) ? (uint16_t)(target + 1U) : CTRL_TASK_ID_MAX;
        }
        if (app_task_set_target_id(target, true) == ESP_OK) {
            char msg[64];
            snprintf(msg, sizeof(msg), "target id => %u", (unsigned)target);
            app_ctrl_set_notice(msg, CTRL_NOTICE_SHOW_MS);
        }
        return;
    }
    if (left_half) {
        if (dock_busy) {
            app_ctrl_set_notice("dock busy, cannot start", CTRL_NOTICE_SHOW_MS);
            return;
        }
        if (app_task_start_local() == ESP_OK) {
            char msg[64];
            snprintf(msg, sizeof(msg), "task armed / target=%u", (unsigned)task->target_id);
            app_ctrl_set_notice(msg, CTRL_NOTICE_SHOW_MS);
        }
        return;
    }
    if (dock_busy) {
        (void)app_ch32_link_send_cmd('S');
        app_task_cancel("manual abort");
        app_ctrl_set_notice("manual abort sent", CTRL_NOTICE_SHOW_MS);
    } else {
        app_task_reset_idle();
        app_ctrl_set_notice("task reset", CTRL_NOTICE_SHOW_MS);
    }
#endif
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
        (void)app_vision_get_latest_result(&vision);
        (void)app_dock_judge_process(&vision, &dock);
        (void)app_task_get_snapshot(&task);
        bool ch32_ready = false;
        bool dock_busy = false;
        bool has_weight = false;
        int32_t weight_g = 0;
        app_ch32_proto_stage_t proto_stage = APP_CH32_STAGE_UNKNOWN;
        uint16_t proto_flags = 0;
        uint8_t proto_error = APP_CH32_ERR_NONE;
        bool cargo_wait_window_seen = false;
        uint32_t last_probe_ms = 0;
        uint32_t busy_deadline_ms = 0;
        uint32_t notice_deadline_ms = 0;
        uint32_t retrigger_deadline_ms = 0;
        char notice[96] = {0};
        uint16_t applied_target_id = 0;
        taskENTER_CRITICAL(&s_ctrl_mux);
        s_rt.ch32_ready = app_ch32_link_is_ready();
        ch32_ready = s_rt.ch32_ready;
        dock_busy = s_rt.dock_busy;
        cargo_wait_window_seen = s_rt.cargo_wait_window_seen;
        has_weight = s_rt.has_weight;
        weight_g = s_rt.last_weight_g;
        proto_stage = s_rt.last_proto_stage;
        proto_flags = s_rt.last_proto_flags;
        proto_error = s_rt.last_proto_error;
        last_probe_ms = s_rt.last_ready_probe_ms;
        busy_deadline_ms = s_rt.busy_deadline_ms;
        notice_deadline_ms = s_rt.notice_deadline_ms;
        retrigger_deadline_ms = s_rt.retrigger_deadline_ms;
        applied_target_id = s_rt.applied_target_id;
        strlcpy(notice, s_rt.notice, sizeof(notice));
        taskEXIT_CRITICAL(&s_ctrl_mux);
        if ((task.target_dirty) || (applied_target_id != task.target_id)) {
            if (app_dock_judge_set_target_id(task.target_id, true) == ESP_OK) {
                taskENTER_CRITICAL(&s_ctrl_mux);
                s_rt.applied_target_id = task.target_id;
                taskEXIT_CRITICAL(&s_ctrl_mux);
                ESP_LOGI(TAG, "applied target id => %u", (unsigned)task.target_id);
            }
        }
        app_ctrl_handle_touch(&task, dock_busy, now_ms);
        if (!ch32_ready && (now_ms - last_probe_ms >= CTRL_READY_PROBE_INTERVAL_MS)) {
            taskENTER_CRITICAL(&s_ctrl_mux);
            s_rt.last_ready_probe_ms = now_ms;
            taskEXIT_CRITICAL(&s_ctrl_mux);
            (void)app_ch32_link_probe_ready(200);
        }
        if (dock_busy && (busy_deadline_ms != 0U) && ((int32_t)(now_ms - busy_deadline_ms) >= 0)) {
            if (app_ctrl_proto_stage_is_cargo_wait_window(proto_stage) || cargo_wait_window_seen) {
                taskENTER_CRITICAL(&s_ctrl_mux);
                app_ctrl_hold_waiting_cargo_locked();
                taskEXIT_CRITICAL(&s_ctrl_mux);
                dock_busy = true;
                cargo_wait_window_seen = true;
                proto_stage = APP_CH32_STAGE_WAITING_CARGO;
                proto_error = APP_CH32_ERR_NONE;
                busy_deadline_ms = 0;
            } else {
                taskENTER_CRITICAL(&s_ctrl_mux);
                s_rt.dock_busy = false;
                s_rt.busy_deadline_ms = 0;
                s_rt.last_proto_error = APP_CH32_ERR_TIMEOUT;
                app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
                app_ctrl_set_notice_locked("dock: CH32 timeout", CTRL_NOTICE_SHOW_MS);
                taskEXIT_CRITICAL(&s_ctrl_mux);
                dock_busy = false;
                proto_error = APP_CH32_ERR_TIMEOUT;
                if (task.active || task.state == APP_TASK_STATE_DOCKING) {
                    app_task_mark_fault("CH32 timeout");
                    (void)app_task_get_snapshot(&task);
                }
            }
        }
        if (!dock_busy &&
            app_ctrl_is_soft_waiting_cargo_error(proto_stage,
                                                 proto_flags,
                                                 proto_stage,
                                                 proto_flags,
                                                 proto_error,
                                                 cargo_wait_window_seen)) {
            taskENTER_CRITICAL(&s_ctrl_mux);
            app_ctrl_hold_waiting_cargo_locked();
            taskEXIT_CRITICAL(&s_ctrl_mux);
            dock_busy = true;
            cargo_wait_window_seen = true;
            proto_stage = APP_CH32_STAGE_WAITING_CARGO;
            proto_error = APP_CH32_ERR_NONE;
            busy_deadline_ms = 0;
        }
        const bool ready_level = (dock.state == APP_DOCK_STATE_READY_TO_DOCK);
        const bool retrigger_blocked = app_ctrl_deadline_active(retrigger_deadline_ms, now_ms);
        if (task.active && (task.state == APP_TASK_STATE_WAIT_APPROACH) && ready_level) {
            app_task_mark_auth_passed(dock.tag_id);
            (void)app_task_get_snapshot(&task);
            app_ctrl_set_notice("auth passed / ready to dock", CTRL_NOTICE_SHOW_MS);
        }
#if CTRL_AUTO_DOCK_ENABLE
        if (task.active && !dock_busy && !retrigger_blocked && !prev_ready_level && ready_level) {
            if (!ch32_ready) {
                app_ctrl_set_notice("dock: ready but CH32 not ready", CTRL_NOTICE_SHOW_MS);
            } else {
                ESP_LOGI(TAG,
                         "READY rising edge -> send @%c (id=%u dx=%ld dy=%ld z=%ld score=%u)",
                         CTRL_DOCK_CMD,
                         (unsigned)dock.tag_id,
                         (long)dock.dx,
                         (long)dock.dy,
                         (long)dock.est_distance_mm,
                         (unsigned)dock.hover_score);
                esp_err_t ret = app_ch32_link_send_cmd_and_wait_ack(CTRL_DOCK_CMD, CTRL_ACK_WAIT_MS);
                if (ret == ESP_OK) {
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
                    dock_busy = true;
                    cargo_wait_window_seen = false;
                    app_task_mark_docking_started();
                    (void)app_task_get_snapshot(&task);
                } else {
                    ESP_LOGW(TAG, "send @%c failed: %s", CTRL_DOCK_CMD, esp_err_to_name(ret));
                    taskENTER_CRITICAL(&s_ctrl_mux);
                    s_rt.last_proto_error = APP_CH32_ERR_INTERNAL;
                    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
                    taskEXIT_CRITICAL(&s_ctrl_mux);
                    app_ctrl_set_notice("dock: CH32 ack timeout", CTRL_NOTICE_SHOW_MS);
                    app_task_mark_fault("CH32 ack timeout");
                    (void)app_task_get_snapshot(&task);
                }
            }
        }
#endif
        if (!prev_dock_busy && dock_busy && task.active && task.state != APP_TASK_STATE_DOCKING) {
            app_task_mark_docking_started();
            (void)app_task_get_snapshot(&task);
        }
        if (prev_dock_busy && !dock_busy) {
            if (proto_error == APP_CH32_ERR_NONE) {
                if (task.state == APP_TASK_STATE_DOCKING || task.active) {
                    app_task_mark_completed("dock cycle done");
                    (void)app_task_get_snapshot(&task);
                }
            } else {
                app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));
                (void)app_task_get_snapshot(&task);
            }
        }
        if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy && (task.active || task.state == APP_TASK_STATE_DOCKING)) {
            app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));
            (void)app_task_get_snapshot(&task);
        }
        prev_ready_level = ready_level;
        prev_dock_busy = dock_busy;
        char status[128] = {0};
        char detail[224] = {0};
        char task_brief[96] = {0};
        app_task_format_brief(&task, task_brief, sizeof(task_brief));
        app_ctrl_compose_task_status(&task, &dock, ch32_ready, status, sizeof(status));
        app_ctrl_compose_detail(&dock, has_weight, weight_g, proto_stage, detail, sizeof(detail));
        if (dock_busy) {
            snprintf(status, sizeof(status), "%s", app_ctrl_proto_stage_status_text(proto_stage));
        }
        if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy) {
            snprintf(status,
                     sizeof(status),
                     "dock: CH32 err %s",
                     app_ch32_link_proto_error_name(proto_error));
        }
        if (retrigger_blocked && !dock_busy && (proto_error == APP_CH32_ERR_NONE) && (notice[0] == '\0')) {
            strlcpy(status, "dock: cooldown / wait next approach", sizeof(status));
        }
        if (app_ctrl_deadline_active(notice_deadline_ms, now_ms) && (notice[0] != '\0')) {
            strlcpy(status, notice, sizeof(status));
        }
        app_ui_set_status(status);
        app_ui_set_vision_text(task_brief);
        app_ui_set_dock_text(detail);
        app_ui_update_hud(&vision, &dock);
        vTaskDelay(pdMS_TO_TICKS(CTRL_POLL_MS));
    }
}
esp_err_t app_ctrl_init(void)
{
    if (s_rt.inited) {
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
    if (!s_rt.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctrl_task != NULL) {
        return ESP_OK;
    }
    BaseType_t ret = xTaskCreatePinnedToCore(app_ctrl_task,
                                             "app_ctrl",
                                             CTRL_TASK_STACK_SIZE,
                                             NULL,
                                             CTRL_TASK_PRIORITY,
                                             &s_ctrl_task,
                                             CTRL_TASK_CORE_ID);
    if (ret != pdPASS) {
        s_ctrl_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ctrl task started");
    return ESP_OK;
}
