#include "app_task.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

// 高层任务状态机
//
// 状态转移路径（正常流）：
//   CONFIGURED → WAIT_APPROACH → AUTH_PASSED → DOCKING → COMPLETED
//   任意状态 → FAULT（CH32 故障/超时）
//   任意状态 → CANCELLED（天气保护/主动取消）
//
// 线程安全：所有状态修改在 s_mux 临界区内完成；事件回调在临界区外触发，
//   回调收到的是状态快照，不持有锁，回调中可以安全调用任何公共接口。
//
// NVS 持久化：target_id 在 set_target_id(persist=true) 时写入，
//   重启后 init() 从 NVS 恢复，保证掉电后仍使用上次配置的标签。

static const char *TAG = "app_task";
static const char *NVS_NS = "sky_task";
static const char *NVS_KEY_TARGET_ID = "target_id";

// 任务状态只在 s_mux 保护下修改，对外一律返回值拷贝快照。
typedef struct {
    bool inited;             // 模块初始化完成。
    bool active;             // 当前是否有未结束任务。
    bool target_dirty;       // 新目标尚未被控制循环应用。
    uint16_t target_id;      // 期望匹配的 AprilTag ID。
    uint16_t matched_tag_id; // 已通过鉴权的标签 ID。
    app_task_state_t state;  // 高层任务阶段。
    char source[20];         // 请求来源，例如 local/cloud。
    char note[64];           // 状态变化原因或完成说明。
    uint32_t state_since_ms; // 进入当前状态的时间。
} app_task_runtime_t;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static app_task_runtime_t s_rt = {0};
#define APP_TASK_MAX_EVENT_CBS 4
typedef struct {
    app_task_event_cb_t cb;
    void *user_ctx;
} app_task_event_slot_t;
static app_task_event_slot_t s_event_cbs[APP_TASK_MAX_EVENT_CBS] = {0};

/* ---------- 锁内状态与事件快照 ---------- */

static uint32_t app_task_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
// 修改任务状态和状态进入时间，必须在 s_mux 内调用。
static void app_task_change_state_locked(app_task_state_t state, const char *note)
{
    s_rt.state = state;
    s_rt.state_since_ms = app_task_now_ms();
    if (note != NULL)
    {
        strlcpy(s_rt.note, note, sizeof(s_rt.note));
    }
    else
    {
        s_rt.note[0] = '\0';
    }
}
static void app_task_copy_snapshot_locked(app_task_snapshot_t *out)
{
    out->inited = s_rt.inited;
    out->active = s_rt.active;
    out->target_dirty = s_rt.target_dirty;
    out->target_id = s_rt.target_id;
    out->matched_tag_id = s_rt.matched_tag_id;
    out->state = s_rt.state;
    out->state_since_ms = s_rt.state_since_ms;
    strlcpy(out->source, s_rt.source, sizeof(out->source));
    strlcpy(out->note, s_rt.note, sizeof(out->note));
}
// 发事件前复制快照和回调表，避免回调中再次访问任务模块造成长时间持锁。
static void app_task_emit_event(app_task_event_t event)
{
    app_task_snapshot_t snap = {0};
    app_task_event_slot_t callbacks[APP_TASK_MAX_EVENT_CBS] = {0};
    taskENTER_CRITICAL(&s_mux);
    app_task_copy_snapshot_locked(&snap);
    memcpy(callbacks, s_event_cbs, sizeof(callbacks));
    taskEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < APP_TASK_MAX_EVENT_CBS; i++)
    {
        if (callbacks[i].cb != NULL)
        {
            callbacks[i].cb(event, &snap, callbacks[i].user_ctx);
        }
    }
}
/* ---------- 目标 ID 持久化 ---------- */

// 将目标标签 ID 保存到 NVS，重启后继续使用上次配置。
static esp_err_t app_task_persist_target_id(uint16_t target_id)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = nvs_set_u32(handle, NVS_KEY_TARGET_ID, (uint32_t)target_id);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}
// 从 NVS 读取目标 ID，失败或越界时回退到默认值。
static uint16_t app_task_load_target_id(uint16_t default_target_id)
{
    nvs_handle_t handle;
    uint32_t value = (uint32_t)default_target_id;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (ret != ESP_OK)
    {
        return default_target_id;
    }
    ret = nvs_get_u32(handle, NVS_KEY_TARGET_ID, &value);
    nvs_close(handle);
    if (ret != ESP_OK || value > UINT16_MAX)
    {
        return default_target_id;
    }
    return (uint16_t)value;
}
// 注册任务事件监听者，最多保留少量固定槽位。
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx)
{
    if (cb == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < APP_TASK_MAX_EVENT_CBS; i++)
    {
        if (s_event_cbs[i].cb == NULL)
        {
            s_event_cbs[i].cb = cb;
            s_event_cbs[i].user_ctx = user_ctx;
            taskEXIT_CRITICAL(&s_mux);
            return ESP_OK;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    return ESP_ERR_NO_MEM;
}
// 初始化任务状态，并广播 INIT 事件给 UI/云端/控制器。
/* ---------- 任务生命周期 ---------- */

esp_err_t app_task_init(uint16_t default_target_id)
{
    taskENTER_CRITICAL(&s_mux);
    if (s_rt.inited)
    {
        taskEXIT_CRITICAL(&s_mux);
        return ESP_OK;
    }
    taskEXIT_CRITICAL(&s_mux);
    uint16_t loaded_target = app_task_load_target_id(default_target_id);
    taskENTER_CRITICAL(&s_mux);
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.inited = true;
    s_rt.target_id = loaded_target;
    strlcpy(s_rt.source, "local", sizeof(s_rt.source));
    app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "configured");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_INIT);
    return ESP_OK;
}
// 更新目标 ID；无活动任务时回到 configured 状态。
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist)
{
    if (!s_rt.inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_mux);
    s_rt.target_id = target_id;
    s_rt.matched_tag_id = 0;
    s_rt.target_dirty = true;
    if (!s_rt.active)
    {
        app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "target updated");
    }
    taskEXIT_CRITICAL(&s_mux);
    if (persist)
    {
        esp_err_t ret = app_task_persist_target_id(target_id);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "persist target id failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    app_task_emit_event(APP_TASK_EVENT_TARGET_UPDATED);
    return ESP_OK;
}
// 启动一单任务，进入等待无人机靠近/认证阶段。
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source)
{
    if (!s_rt.inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_mux);
    s_rt.target_id = target_id;
    s_rt.target_dirty = true;
    s_rt.active = true;
    s_rt.matched_tag_id = 0;
    strlcpy(s_rt.source, (source != NULL) ? source : "local", sizeof(s_rt.source));
    app_task_change_state_locked(APP_TASK_STATE_WAIT_APPROACH, "waiting target approach");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    return ESP_OK;
}
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source)
{
    return app_task_start_with_target(target_id, (source != NULL) ? source : "remote");
}
// AprilTag 对接判定 ready 后标记认证通过。
void app_task_mark_auth_passed(uint16_t matched_tag_id)
{
    bool changed = false;
    taskENTER_CRITICAL(&s_mux);
    if (s_rt.active && s_rt.state == APP_TASK_STATE_WAIT_APPROACH)
    {
        s_rt.matched_tag_id = matched_tag_id;
        app_task_change_state_locked(APP_TASK_STATE_AUTH_PASSED, "auth passed");
        changed = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (changed)
    {
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    }
}
// CH32 接收对接命令或上报 busy 后进入 docking。
void app_task_mark_docking_started(void)
{
    bool changed = false;
    taskENTER_CRITICAL(&s_mux);
    if (s_rt.active &&
        (s_rt.state == APP_TASK_STATE_WAIT_APPROACH || s_rt.state == APP_TASK_STATE_AUTH_PASSED))
    {
        app_task_change_state_locked(APP_TASK_STATE_DOCKING, "docking in progress");
        changed = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (changed)
    {
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    }
}
// 对接流程完成，任务转为非 active。
void app_task_mark_completed(const char *note)
{
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    app_task_change_state_locked(APP_TASK_STATE_COMPLETED, note != NULL ? note : "task completed");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
void app_task_mark_fault(const char *note)
{
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    app_task_change_state_locked(APP_TASK_STATE_FAULT, note != NULL ? note : "task fault");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
void app_task_cancel(const char *note)
{
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    s_rt.matched_tag_id = 0;
    app_task_change_state_locked(APP_TASK_STATE_CANCELLED, note != NULL ? note : "task cancelled");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
// get_snapshot 同时消费 target_dirty 标记（读后清零），
// 供控制循环调用——控制循环负责把新 target_id 同步给 dock_judge。
bool app_task_get_snapshot(app_task_snapshot_t *out)
{
    if (out == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    app_task_copy_snapshot_locked(out);
    s_rt.target_dirty = false;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}
// peek_snapshot 只读，不清 target_dirty，适合 UI/云端轮询状态展示。
bool app_task_peek_snapshot(app_task_snapshot_t *out)
{
    if (out == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    app_task_copy_snapshot_locked(out);
    taskEXIT_CRITICAL(&s_mux);
    return true;
}
/* ---------- 状态文案 ---------- */

const char *app_task_state_to_text(app_task_state_t state)
{
    switch (state) {
    case APP_TASK_STATE_IDLE:          return "idle";
    case APP_TASK_STATE_CONFIGURED:    return "configured";
    case APP_TASK_STATE_WAIT_APPROACH: return "wait_approach";
    case APP_TASK_STATE_AUTH_PASSED:   return "auth_passed";
    case APP_TASK_STATE_DOCKING:       return "docking";
    case APP_TASK_STATE_COMPLETED:     return "completed";
    case APP_TASK_STATE_FAULT:         return "fault";
    case APP_TASK_STATE_CANCELLED:     return "cancelled";
    default:                           return "unknown";
    }
}
// 生成一行紧凑任务状态，供 HUD/主屏复用。
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len)
{
    if (snap == NULL || buf == NULL || buf_len == 0U)
    {
        return;
    }
    if (!snap->inited)
    {
        snprintf(buf, buf_len, "task: not init");
        return;
    }
    snprintf(buf,
        buf_len,
        "task:%s target:%u src:%s",
        app_task_state_to_text(snap->state),
        (unsigned)snap->target_id,
        snap->source[0] != '\0' ? snap->source : "local");
}
