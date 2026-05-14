/* 实现说明：任务状态的所有变化都集中从本模块流转，方便 UI 和云端同步。 */
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
#include "app_drone_ai.h"

static const char *TAG = "app_task";
static const char *NVS_NS = "sky_task";
static const char *NVS_KEY_TARGET_ID = "target_id";

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool inited;                    /* 任务模块是否已经初始化。 */
    bool active;                    /* 当前是否有任务正在执行。 */
    bool target_dirty;              /* 目标 tag ID 是否发生过更新。 */
    uint16_t target_id;             /* 当前任务或配置中的目标 tag ID。 */
    uint16_t matched_tag_id;        /* 鉴权通过时匹配到的 tag ID。 */
    app_task_state_t state;         /* 当前任务状态。 */
    char source[20];                /* 任务来源，例如 local、cloud 或 ui。 */
    char note[64];                  /* 当前状态附带的简短说明。 */
    uint32_t state_since_ms;        /* 进入当前状态时的毫秒时间戳。 */
} app_task_runtime_t;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static app_task_runtime_t s_rt = {0};
#define APP_TASK_MAX_EVENT_CBS 4
static struct {
    app_task_event_cb_t cb;
    void *user_ctx;
} s_event_cbs[APP_TASK_MAX_EVENT_CBS] = {0};

/* -------------------------------------------------------------------------- */
/* 状态辅助函数                                                               */
/* -------------------------------------------------------------------------- */

/* 获取当前系统毫秒时间，用作任务状态时间戳。 */
static uint32_t app_task_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/* 在已持锁状态下切换任务状态，并更新说明文本。 */
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

/* 在已持锁状态下把内部运行状态拷贝成外部快照。 */
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

/* 向已注册回调广播一次任务事件和当前快照。 */
static void app_task_emit_event(app_task_event_t event)
{
    app_task_snapshot_t snap = {0};

    taskENTER_CRITICAL(&s_mux);
    app_task_copy_snapshot_locked(&snap);
    taskEXIT_CRITICAL(&s_mux);

    for (int i = 0; i < APP_TASK_MAX_EVENT_CBS; i++)
    {
        if (s_event_cbs[i].cb != NULL)
        {
            s_event_cbs[i].cb(event, &snap, s_event_cbs[i].user_ctx);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 持久化                                                                 */
/* -------------------------------------------------------------------------- */

/* 将目标 tag ID 写入 NVS，供下次启动恢复。 */
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
/* 从 NVS 读取目标 tag ID，失败时回退到默认值。 */
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

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

/* 注册任务状态变化回调，通常由云端模块用于发布状态。 */
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx)
{
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
/* 初始化任务模块，恢复目标 ID 并进入 CONFIGURED 状态。 */
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
    ESP_LOGI(TAG, "task init done, target_id=%u", (unsigned)loaded_target);
    app_task_emit_event(APP_TASK_EVENT_INIT);
    return ESP_OK;
}
/* 更新目标 tag ID；如果当前没有活动任务，则回到 CONFIGURED 状态。 */
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
    ESP_LOGI(TAG, "target_id set to %u", (unsigned)target_id);
    app_task_emit_event(APP_TASK_EVENT_TARGET_UPDATED);
    return ESP_OK;
}
/* 启动一单任务，进入等待目标靠近的 WAIT_APPROACH 状态。 */
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
    ESP_LOGI(TAG, "task started, target_id=%u source=%s", (unsigned)target_id, (source != NULL) ? source : "local");
    app_drone_ai_reset_gate();
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    return ESP_OK;
}
/* 远程任务提交入口，本质上是带 remote/emqx 来源的任务启动。 */
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source)
{
    return app_task_start_with_target(target_id, (source != NULL) ? source : "remote");
}
/* 视觉确认目标后，将任务从 WAIT_APPROACH 推进到 AUTH_PASSED。 */
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
        ESP_LOGI(TAG, "auth passed, matched_tag_id=%u", (unsigned)matched_tag_id);
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    }
}
/* 接驳动作开始后，将任务推进到 DOCKING。 */
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
/* 接驳流程完成后结束活动任务，并进入 COMPLETED。 */
void app_task_mark_completed(const char *note)
{
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    app_task_change_state_locked(APP_TASK_STATE_COMPLETED, note != NULL ? note : "task completed");
    taskEXIT_CRITICAL(&s_mux);
    app_drone_ai_reset_gate();
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/* 出现超时、拒绝或硬件错误时结束活动任务，并进入 FAULT。 */
void app_task_mark_fault(const char *note)
{
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    app_task_change_state_locked(APP_TASK_STATE_FAULT, note != NULL ? note : "task fault");
    taskEXIT_CRITICAL(&s_mux);
    app_drone_ai_reset_gate();
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/* 主动取消当前任务，并清除已匹配的 tag ID。 */
void app_task_cancel(const char *note)
{
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    s_rt.matched_tag_id = 0;
    app_task_change_state_locked(APP_TASK_STATE_CANCELLED, note != NULL ? note : "task cancelled");
    taskEXIT_CRITICAL(&s_mux);
    app_drone_ai_reset_gate();
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/* 读取任务快照；读取后会清除 target_dirty 标记。 */
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

/* 读取任务快照但不消费 target_dirty，给摄像头等旁路门控使用。 */
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

/* 将任务状态枚举转换成 MQTT/UI 友好的短文本。 */
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
/* 将任务快照压缩成一行 UI 状态栏文本。 */
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
