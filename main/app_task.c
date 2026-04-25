/*
 * app_task.c - 接驳任务状态管理模块（详细注释版）
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

#include "app_task.h"                              // 项目自定义模块头文件，声明 app_task 对外提供的接口。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "nvs.h"
static const char *TAG = "app_task";                             // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
static const char *NVS_NS = "sky_task";
static const char *NVS_KEY_TARGET_ID = "target_id";
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    bool inited;                 // 模块是否已经初始化，避免重复初始化覆盖当前任务状态。
    bool active;                 // 当前是否有任务正在执行，用于判断能否进入空闲/配置态。
    bool target_dirty;           // 目标 tag ID 是否被更新过，提醒 UI/云端刷新配置。
    uint16_t target_id;          // 本次任务期望识别或接驳的目标 tag ID。
    uint16_t matched_tag_id;     // 实际识别匹配到的 tag ID，任务开始前通常为 0。
    app_task_state_t state;      // 当前任务状态，例如已配置、鉴权通过、接驳中、完成等。
    char source[20];             // 当前目标配置来源，例如 local、cloud、ui。
    char note[64];               // 状态变化的简短说明，便于日志、UI 或云端展示。
    uint32_t state_since_ms;     // 进入当前状态时的毫秒时间戳，用于计算状态持续时间。
} app_task_runtime_t;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;        // 模块级静态变量 s_mux，只在本文件内部使用，避免被其他文件直接修改。
static app_task_runtime_t s_rt = {0};                            // 模块级静态变量 s_rt，只在本文件内部使用，避免被其他文件直接修改。
static app_task_event_cb_t s_event_cb = NULL;                    // 模块级静态变量 s_event_cb，只在本文件内部使用，避免被其他文件直接修改。
static void *s_event_user_ctx = NULL;                            // 模块级静态变量 s_event_user_ctx，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 获取当前毫秒时间，用于记录任务状态变化时间。
 */
static uint32_t app_task_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/*
 * 在持锁状态下切换任务状态并记录说明文本。
 */
static void app_task_change_state_locked(app_task_state_t state, const char *note)
{
    s_rt.state = state;
    s_rt.state_since_ms = app_task_now_ms();
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (note != NULL) {
        strlcpy(s_rt.note, note, sizeof(s_rt.note));
    } else {
        s_rt.note[0] = '\0';
    }
}
/*
 * 触发任务事件回调，让云端或其他模块知道任务状态变化。
 */
static void app_task_emit_event(app_task_event_t event)
{
    app_task_event_cb_t cb = NULL;
    void *user_ctx = NULL;
    app_task_snapshot_t snap = {0};

    /*
     * 先在临界区内把回调指针和运行时状态复制到局部变量。
     * 这样可以保证快照里的字段来自同一时刻，避免其他任务正在修改 s_rt 时读到一半新一半旧的数据。
     */
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);
    cb = s_event_cb;
    user_ctx = s_event_user_ctx;

    // 将内部运行时状态转换成对外只读快照，回调接收的是 snap 而不是直接访问 s_rt。
    snap.inited = s_rt.inited;
    snap.active = s_rt.active;
    snap.target_dirty = s_rt.target_dirty;
    snap.target_id = s_rt.target_id;
    snap.matched_tag_id = s_rt.matched_tag_id;
    snap.state = s_rt.state;
    snap.state_since_ms = s_rt.state_since_ms;
    strlcpy(snap.source, s_rt.source, sizeof(snap.source));
    strlcpy(snap.note, s_rt.note, sizeof(snap.note));
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);

    /*
     * 回调可能做日志、发云端消息，甚至间接调用本模块接口。
     * 所以必须在退出临界区之后再执行，避免长时间关中断或造成锁重入问题。
     */
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (cb != NULL) {
        cb(event, &snap, user_ctx);
    }
}
/*
 * 把目标 tag ID 保存到 NVS，断电重启后仍能保留。
 */
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
/*
 * 从 NVS 读取目标 tag ID，如果没有保存过则使用默认值。
 */
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
/*
 * 注册任务事件回调，常用于任务变化后立即上报云端。
 */
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx)
{
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);
    s_event_cb = cb;
    s_event_user_ctx = user_ctx;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 初始化任务管理模块，加载默认目标 ID 并进入空闲状态。
 */
esp_err_t app_task_init(uint16_t default_target_id)
{
    /*
     * 初始化函数可能被重复调用。
     * 先在临界区内检查 inited，避免多任务场景下重复清空任务状态。
     */
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);
    if (s_rt.inited) {
        // 退出临界区：共享变量访问结束，恢复正常调度/中断。
        taskEXIT_CRITICAL(&s_mux);
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);

    /*
     * 从 NVS 加载上次保存的目标 tag ID。
     * 如果 NVS 没有记录或读取失败，就使用 main.c 传进来的默认 ID。
     */
    uint16_t loaded_target = app_task_load_target_id(default_target_id);

    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);

    /*
     * 初始化运行时状态。
     * 启动后默认进入 CONFIGURED，而不是 IDLE，
     * 表示系统已经有一个可用目标 ID，只是在等待任务开始。
     */
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.inited = true;
    s_rt.target_id = loaded_target;
    strlcpy(s_rt.source, "local", sizeof(s_rt.source));
    app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "configured");
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "task init done, target_id=%u", (unsigned)loaded_target);
    app_task_emit_event(APP_TASK_EVENT_INIT);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 线程安全读取当前目标 tag ID。
 */
uint16_t app_task_get_target_id(void)
{
    uint16_t value = 0;
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);
    value = s_rt.target_id;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);
    return value;
}
/*
 * 设置目标 tag ID，并可选择是否持久化到 NVS。
 */
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist)
{
    /*
     * 任务模块必须先初始化，才能修改目标 ID。
     */
    if (!s_rt.inited) {
        return ESP_ERR_INVALID_STATE;
    }

    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);

    /*
     * 更新目标 ID 后，清掉已经匹配过的 tag。
     * target_dirty 用来告诉 UI/云端：目标配置发生了变化。
     */
    s_rt.target_id = target_id;
    s_rt.matched_tag_id = 0;
    s_rt.target_dirty = true;

    /*
     * 如果当前没有活跃任务，修改目标 ID 后保持在 CONFIGURED 状态。
     * 如果任务正在进行，则只更新目标，不强行打断当前状态。
     */
    if (!s_rt.active) {
        app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "target updated");
    }
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);
    if (persist) {
        /*
         * 需要持久化时写入 NVS。
         * 失败时返回错误，但内存中的 target_id 已经更新，系统仍可继续运行。
         */
        esp_err_t ret = app_task_persist_target_id(target_id);
        if (ret != ESP_OK) {
            // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
            ESP_LOGW(TAG, "persist target id failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "target_id set to %u", (unsigned)target_id);
    app_task_emit_event(APP_TASK_EVENT_TARGET_UPDATED);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 以指定目标 tag ID 开始一单接驳任务。
 */
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source)
{
    /*
     * 开始任务前必须完成 app_task_init()。
     */
    if (!s_rt.inited) {
        return ESP_ERR_INVALID_STATE;
    }

    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_mux);

    /*
     * 一单新任务开始：
     * - 设置目标 tag ID；
     * - 标记 active；
     * - 清空 matched_tag_id；
     * - 记录来源 local/remote/touch；
     * - 进入 WAIT_APPROACH，等待无人机靠近并通过视觉鉴权。
     */
    s_rt.target_id = target_id;
    s_rt.target_dirty = true;
    s_rt.active = true;
    s_rt.matched_tag_id = 0;
    strlcpy(s_rt.source, (source != NULL) ? source : "local", sizeof(s_rt.source));
    app_task_change_state_locked(APP_TASK_STATE_WAIT_APPROACH, "waiting target approach");
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_mux);
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "task started, target_id=%u source=%s", (unsigned)target_id, (source != NULL) ? source : "local");
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 从本地触摸屏/按钮触发任务开始。
 */
esp_err_t app_task_start_local(void)
{
    return app_task_start_with_target(app_task_get_target_id(), "touch");
}
/*
 * 从云端命令触发接驳任务，记录来源和请求信息。
 */
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source)
{
    return app_task_start_with_target(target_id, (source != NULL) ? source : "remote");
}
/*
 * 标记视觉鉴权通过，说明已识别到正确无人机。
 */
void app_task_mark_auth_passed(uint16_t matched_tag_id)
{
    bool changed = false;

    /*
     * 只有活跃任务且处于 WAIT_APPROACH 时，视觉鉴权通过才会改变任务状态。
     * 这样可以避免重复调用把已完成/已取消任务重新改成 auth_passed。
     */
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
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
/*
 * 标记机电接驳流程已经开始。
 */
void app_task_mark_docking_started(void)
{
    bool changed = false;

    /*
     * 只有等待靠近或鉴权通过后的任务，才能进入 DOCKING。
     */
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
/*
 * 标记任务完成。
 */
void app_task_mark_completed(const char *note)
{
    /*
     * 完成后 active=false，表示这一单任务已经闭环。
     */
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    app_task_change_state_locked(APP_TASK_STATE_COMPLETED, note != NULL ? note : "task completed");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/*
 * 标记任务故障。
 */
void app_task_mark_fault(const char *note)
{
    /*
     * 故障状态同样结束当前 active 任务。
     * note 会被云端状态上报带出去，便于小程序/服务器显示原因。
     */
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    app_task_change_state_locked(APP_TASK_STATE_FAULT, note != NULL ? note : "task fault");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/*
 * 取消当前任务。
 */
void app_task_cancel(const char *note)
{
    /*
     * 取消任务时清掉 matched_tag_id，避免下一单任务误用上一单鉴权结果。
     */
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    s_rt.matched_tag_id = 0;
    app_task_change_state_locked(APP_TASK_STATE_CANCELLED, note != NULL ? note : "task cancelled");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/*
 * 强制回到空闲状态。
 */
void app_task_reset_idle(void)
{
    /*
     * reset_idle 实际回到 CONFIGURED。
     * 这样目标 ID 仍然保留，系统可以直接开始下一单任务。
     */
    taskENTER_CRITICAL(&s_mux);
    s_rt.active = false;
    s_rt.matched_tag_id = 0;
    app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "configured");
    taskEXIT_CRITICAL(&s_mux);
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
}
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
bool app_task_get_snapshot(app_task_snapshot_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (out == NULL) {
        return false;
    }

    /*
     * 快照是跨模块通信用的只读副本。
     * app_cloud.c 会把它转成 MQTT JSON，app_ctrl.c/UI 也可以用它显示任务状态。
     */
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

    /*
     * target_dirty 被读取后清除，表示“目标变更”已经被观察到。
     */
    s_rt.target_dirty = false;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}
/*
 * 把任务状态枚举转成可读字符串。
 */
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
/*
 * 把任务快照格式化成一行简短文本，适合显示或日志打印。
 */
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
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
