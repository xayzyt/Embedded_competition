/*
 * app_task.c - 接驳任务状态管理模块
 *
 * 这个文件负责维护“当前是否有一单任务正在进行、目标 tag ID 是多少、任务到了哪个阶段”。
 * 它比 app_ctrl.c 更偏业务层：
 * - 任务开始、鉴权通过、开始接驳、完成、故障、取消；
 * - 保存/读取目标 tag ID；
 * - 生成任务快照供 UI 或云端发布；
 * - 通过回调通知 app_cloud.c 等模块任务状态变化。
 *
 * 可以把它理解成订单/任务管理器，app_ctrl.c 则是实际执行流程控制器。
 */

#include "app_task.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "app_task";
static const char *NVS_NS = "sky_task";
static const char *NVS_KEY_TARGET_ID = "target_id";
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
} app_task_runtime_t;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static app_task_runtime_t s_rt = {0};
static app_task_event_cb_t s_event_cb = NULL;
static void *s_event_user_ctx = NULL;
static uint32_t app_task_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
static void app_task_change_state_locked(app_task_state_t state, const char *note)
{
    s_rt.state = state;
    s_rt.state_since_ms = app_task_now_ms();
    if (note != NULL) {
        strlcpy(s_rt.note, note, sizeof(s_rt.note));
    } else {
        s_rt.note[0] = '\0';
    }
}
static void app_task_emit_event(app_task_event_t event)
{
    app_task_event_cb_t cb = NULL;
    void *user_ctx = NULL;
    app_task_snapshot_t snap = {0};

    taskENTER_CRITICAL(&s_mux);
    cb = s_event_cb;
    user_ctx = s_event_user_ctx;

    snap.inited = s_rt.inited;
    snap.active = s_rt.active;
    snap.target_dirty = s_rt.target_dirty;
    snap.target_id = s_rt.target_id;
    snap.matched_tag_id = s_rt.matched_tag_id;
    snap.state = s_rt.state;
    snap.state_since_ms = s_rt.state_since_ms;
    strlcpy(snap.source, s_rt.source, sizeof(snap.source));
    strlcpy(snap.note, s_rt.note, sizeof(snap.note));

    taskEXIT_CRITICAL(&s_mux);
    if (cb != NULL) {
        cb(event, &snap, user_ctx);
    }
}
static esp_err_t app_task_persist_target_id(uint16_t target_id)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_u32(handle, NVS_KEY_TARGET_ID, (uint32_t)target_id);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}
static uint16_t app_task_load_target_id(uint16_t default_target_id)
{
    nvs_handle_t handle;
    uint32_t value = (uint32_t)default_target_id;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return default_target_id;
    }
    ret = nvs_get_u32(handle, NVS_KEY_TARGET_ID, &value);
    nvs_close(handle);
    if (ret != ESP_OK || value > UINT16_MAX) {
        return default_target_id;
    }
    return (uint16_t)value;
}
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx)
{

    taskENTER_CRITICAL(&s_mux);
    s_event_cb = cb;
    s_event_user_ctx = user_ctx;

    taskEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}
esp_err_t app_task_init(uint16_t default_target_id)
{

    taskENTER_CRITICAL(&s_mux);
    if (s_rt.inited) {

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
    ESP_LOGI(TAG, "task init done, target_id=%u", (unsigned)loaded_target);
    app_task_emit_event(APP_TASK_EVENT_INIT);
    return ESP_OK;
}
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist)
{
    if (!s_rt.inited) {
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_mux);
    s_rt.target_id = target_id;
    s_rt.matched_tag_id = 0;
    s_rt.target_dirty = true;
    if (!s_rt.active) {
        app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "target updated");
    }

    taskEXIT_CRITICAL(&s_mux);
    if (persist) {
        esp_err_t ret = app_task_persist_target_id(target_id);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "persist target id failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    ESP_LOGI(TAG, "target_id set to %u", (unsigned)target_id);
    app_task_emit_event(APP_TASK_EVENT_TARGET_UPDATED);
    return ESP_OK;
}
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source)
{
    if (!s_rt.inited) {
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
    ESP_LOGI(TAG, "task started, target_id=%u source=%s", (unsigned)target_id, (source != NULL) ? source : "local");
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    return ESP_OK;
}
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source)
{
    return app_task_start_with_target(target_id, (source != NULL) ? source : "remote");
}
void app_task_mark_auth_passed(uint16_t matched_tag_id)
{
    bool changed = false;

    taskENTER_CRITICAL(&s_mux);
    if (s_rt.active && s_rt.state == APP_TASK_STATE_WAIT_APPROACH) {
        s_rt.matched_tag_id = matched_tag_id;
        app_task_change_state_locked(APP_TASK_STATE_AUTH_PASSED, "auth passed");
        changed = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (changed) {
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    }
}
void app_task_mark_docking_started(void)
{
    bool changed = false;
    taskENTER_CRITICAL(&s_mux);
    if (s_rt.active &&
        (s_rt.state == APP_TASK_STATE_WAIT_APPROACH || s_rt.state == APP_TASK_STATE_AUTH_PASSED)) {
        app_task_change_state_locked(APP_TASK_STATE_DOCKING, "docking in progress");
        changed = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (changed) {
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    }
}
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
bool app_task_get_snapshot(app_task_snapshot_t *out)
{
    if (out == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    out->inited = s_rt.inited;
    out->active = s_rt.active;
    out->target_dirty = s_rt.target_dirty;
    out->target_id = s_rt.target_id;
    out->matched_tag_id = s_rt.matched_tag_id;
    out->state = s_rt.state;
    out->state_since_ms = s_rt.state_since_ms;
    strlcpy(out->source, s_rt.source, sizeof(out->source));
    strlcpy(out->note, s_rt.note, sizeof(out->note));
    s_rt.target_dirty = false;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}
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
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len)
{
    if (snap == NULL || buf == NULL || buf_len == 0U) {
        return;
    }
    if (!snap->inited) {
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