#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    APP_TASK_STATE_IDLE = 0,
    APP_TASK_STATE_CONFIGURED,
    APP_TASK_STATE_WAIT_APPROACH,
    APP_TASK_STATE_AUTH_PASSED,
    APP_TASK_STATE_DOCKING,
    APP_TASK_STATE_COMPLETED,
    APP_TASK_STATE_FAULT,
    APP_TASK_STATE_CANCELLED,
} app_task_state_t;
typedef struct {
    bool inited;
    bool active;
    bool target_dirty;
    uint16_t target_id;
    uint16_t matched_tag_id;
    app_task_state_t state;
    char source[20];
    char note[64];
    uint32_t state_since_ms;
} app_task_snapshot_t;
typedef enum {
    APP_TASK_EVENT_INIT = 0,
    APP_TASK_EVENT_TARGET_UPDATED,
    APP_TASK_EVENT_STATE_CHANGED,
} app_task_event_t;
typedef void (*app_task_event_cb_t)(app_task_event_t event,
                                    const app_task_snapshot_t *snap,
                                    void *user_ctx);
esp_err_t app_task_init(uint16_t default_target_id);
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist);
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source);
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source);
void app_task_mark_auth_passed(uint16_t matched_tag_id);
void app_task_mark_docking_started(void);
void app_task_mark_completed(const char *note);
void app_task_mark_fault(const char *note);
void app_task_cancel(const char *note);
bool app_task_get_snapshot(app_task_snapshot_t *out);
bool app_task_peek_snapshot(app_task_snapshot_t *out);
const char *app_task_state_to_text(app_task_state_t state);
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len);
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx);
#ifdef __cplusplus
}
#endif
