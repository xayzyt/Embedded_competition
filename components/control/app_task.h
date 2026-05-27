#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// 高层配送任务状态。
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
// 任务模块对外暴露的快照。
typedef struct {
    bool inited;                  // 模块是否初始化。
    bool active;                  // 是否存在进行中的任务。
    bool target_dirty;            // 目标 ID 是否需要持久化/同步。
    uint16_t target_id;           // 当前任务目标标签 ID。
    uint16_t matched_tag_id;      // 已认证通过的标签 ID。
    app_task_state_t state;       // 当前任务状态。
    char source[20];              // 任务来源，例如 local/cloud。
    char note[64];                // 状态附加说明。
    uint32_t state_since_ms;      // 进入当前状态的时间戳。
} app_task_snapshot_t;
// 任务事件，用于 UI、云端和控制器同步。
typedef enum {
    APP_TASK_EVENT_INIT = 0,
    APP_TASK_EVENT_TARGET_UPDATED,
    APP_TASK_EVENT_STATE_CHANGED,
} app_task_event_t;
// 任务事件回调。
typedef void (*app_task_event_cb_t)(app_task_event_t event,
                                    const app_task_snapshot_t *snap,
                                    void *user_ctx);
// 初始化、配置目标和启动任务。
esp_err_t app_task_init(uint16_t default_target_id);
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist);
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source);
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source);
// 控制流程中的状态推进接口。
void app_task_mark_auth_passed(uint16_t matched_tag_id);
void app_task_mark_docking_started(void);
void app_task_mark_completed(const char *note);
void app_task_mark_fault(const char *note);
void app_task_cancel(const char *note);
// 读取快照；get 会加锁，peek 保持轻量读取语义。
bool app_task_get_snapshot(app_task_snapshot_t *out);
bool app_task_peek_snapshot(app_task_snapshot_t *out);
// 文案格式化和事件注册。
const char *app_task_state_to_text(app_task_state_t state);
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len);
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx);
#ifdef __cplusplus
}
#endif
