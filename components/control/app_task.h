#pragma once

/*
 * 高层任务状态存储模块。
 * 记录目标 ID、任务生命周期，并向 UI/云端广播快照。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_TASK_STATE_IDLE = 0,         // 空闲状态，当前没有任务，也没有正在等待执行的目标。
    APP_TASK_STATE_CONFIGURED,       // 已配置目标 tag ID，等待任务开始。
    APP_TASK_STATE_WAIT_APPROACH,    // 任务已开始，等待设备靠近或识别目标。
    APP_TASK_STATE_AUTH_PASSED,      // 目标鉴权或匹配通过，可以进入接驳流程。
    APP_TASK_STATE_DOCKING,          // 正在执行接驳动作。
    APP_TASK_STATE_COMPLETED,        // 任务已完成。
    APP_TASK_STATE_FAULT,            // 任务发生故障，需要上层处理或人工干预。
    APP_TASK_STATE_CANCELLED,        // 任务已取消。
} app_task_state_t;

typedef struct {
    bool inited;                     // app_task 模块是否已经初始化。
    bool active;                     // 当前是否有任务正在执行。
    bool target_dirty;               // 目标 tag ID 是否更新过，供 UI/云端判断是否需要刷新。
    uint16_t target_id;              // 当前配置的目标 tag ID。
    uint16_t matched_tag_id;         // 实际识别匹配到的 tag ID，未匹配时为 0。
    app_task_state_t state;          // 当前任务状态。
    char source[20];                 // 目标配置来源，例如 local、cloud、ui。
    char note[64];                   // 当前状态或最近一次状态变化的简短说明。
    uint32_t state_since_ms;         // 进入当前状态时的毫秒时间戳。
} app_task_snapshot_t;

typedef enum {
    APP_TASK_EVENT_INIT = 0,         // 模块完成初始化。
    APP_TASK_EVENT_TARGET_UPDATED,   // 目标 tag ID 被更新。
    APP_TASK_EVENT_STATE_CHANGED,    // 任务状态发生变化。
} app_task_event_t;

/* 任务状态变化时通知云端等模块的回调类型。 */
typedef void (*app_task_event_cb_t)(app_task_event_t event,
                                    const app_task_snapshot_t *snap,
                                    void *user_ctx);

/* 初始化任务模块，读取或设置默认目标 tag ID。 */
esp_err_t app_task_init(uint16_t default_target_id);

/* 更新当前目标 tag ID，可选择写入 NVS 持久保存。 */
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist);

/* 以指定目标和来源启动一单任务。 */
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source);

/* 云端/远程入口：提交一单远程任务请求。 */
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source);

/* 标记目标鉴权通过，并记录实际匹配到的 tag ID。 */
void app_task_mark_auth_passed(uint16_t matched_tag_id);

/* 标记任务已经进入接驳执行阶段。 */
void app_task_mark_docking_started(void);

/* 标记任务完成，并写入完成说明。 */
void app_task_mark_completed(const char *note);

/* 标记任务故障，并写入故障说明。 */
void app_task_mark_fault(const char *note);

/* 取消当前任务，并写入取消说明。 */
void app_task_cancel(const char *note);

/* 读取当前任务快照，供控制、UI 和云端使用。 */
bool app_task_get_snapshot(app_task_snapshot_t *out);

/* 读取当前任务快照，但不清除 target_dirty 标记。 */
bool app_task_peek_snapshot(app_task_snapshot_t *out);

/* 将任务状态枚举转换成短字符串。 */
const char *app_task_state_to_text(app_task_state_t state);

/* 将任务快照格式化成一行简短状态文本。 */
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len);

/* 注册任务事件回调，用于状态变化后通知其他模块。 */
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
