#include "app_ctrl.h"

#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_ctrl_runtime.h"
#include "app_ctrl_ui.h"
#include "app_dock_judge.h"
#include "app_drone_ai.h"
#include "app_task.h"
#include "app_vision.h"

// 自动对接主循环：读取视觉和任务状态，完成判定后更新机械控制与 UI。
static const char *TAG = "app_ctrl";

#define CTRL_TASK_STACK_SIZE (7 * 1024)
#define CTRL_TASK_PRIORITY   5
#define CTRL_TASK_CORE_ID    1
#define CTRL_POLL_MS         60U

// 保存上一轮状态，用于识别门控、READY 和 CH32 busy 的变化边沿。
typedef struct {
    bool prev_ready_level;
    bool prev_dock_busy;
    bool prev_apriltag_enabled;
    uint32_t gate_open_vision_seq;
} app_ctrl_loop_history_t;

static TaskHandle_t s_ctrl_task = NULL;
static bool s_task_callback_registered = false;

static uint32_t app_ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void app_ctrl_on_task_event(app_task_event_t event,
    const app_task_snapshot_t *snap,
    void *user_ctx)
{
    (void)user_ctx;
    if (event != APP_TASK_EVENT_STATE_CHANGED || snap == NULL)
    {
        return;
    }

    // 任务切换时清空 AI 连续命中，防止上一单结果影响下一单。
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

// 只有 AI 已确认无人机且任务正在等待靠近时才开启 AprilTag。
static bool app_ctrl_apriltag_enabled(const app_task_snapshot_t *task)
{
    return task->active &&
           task->state == APP_TASK_STATE_WAIT_APPROACH &&
           app_drone_ai_is_drone_confirmed();
}

static void app_ctrl_apply_vision_gate(app_ctrl_loop_history_t *history,
    bool apriltag_enabled,
    app_vision_result_t *vision)
{
    const bool gate_closed = !apriltag_enabled;
    const bool gate_just_opened = apriltag_enabled && !history->prev_apriltag_enabled;
    if (gate_closed || gate_just_opened)
    {
        history->gate_open_vision_seq = vision->frame_seq;
    }

    // 门控刚打开时丢弃旧视觉结果，等待门控开启后采集的新帧。
    const bool vision_is_stale = vision->frame_seq <= history->gate_open_vision_seq;
    if (gate_closed || gate_just_opened || vision_is_stale)
    {
        memset(vision, 0, sizeof(*vision));
        app_dock_judge_reset();
        history->prev_ready_level = false;
    }
}

static void app_ctrl_task(void *arg)
{
    (void)arg;
    app_ctrl_loop_history_t history = {0};

    for (;;)
    {
        app_ctrl_cycle_t cycle = {
            .now_ms = app_ctrl_now_ms(),
            .prev_ready_level = history.prev_ready_level,
            .prev_dock_busy = history.prev_dock_busy,
        };

        // 每轮读取一次最新快照，保证本轮判断使用同一组输入。
        (void)app_vision_get_latest_result(&cycle.vision);
        (void)app_task_get_snapshot(&cycle.task);

        // AI 未确认时不使用 AprilTag 结果。
        cycle.apriltag_enabled = app_ctrl_apriltag_enabled(&cycle.task);
        app_ctrl_apply_vision_gate(&history, cycle.apriltag_enabled, &cycle.vision);
        (void)app_dock_judge_process(&cycle.vision, &cycle.dock);

        // READY 从 false 变为 true 时才触发一次自动接驳。
        cycle.ready_level = cycle.dock.state == APP_DOCK_STATE_READY_TO_DOCK;
        app_ctrl_runtime_step(&cycle);
        app_ctrl_ui_publish(&cycle);

        history.prev_ready_level = cycle.ready_level;
        history.prev_dock_busy = cycle.runtime.dock_busy;
        history.prev_apriltag_enabled = cycle.apriltag_enabled;
        vTaskDelay(pdMS_TO_TICKS(CTRL_POLL_MS));
    }
}

// 将 CH32 接收任务解析出的消息交给控制状态模块处理。
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx)
{
    (void)user_ctx;
    app_ctrl_runtime_on_ch32_line(msg);
}

// 初始化机械状态，并监听任务切换事件。
esp_err_t app_ctrl_init(void)
{
    esp_err_t ret = app_ctrl_runtime_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    if (s_task_callback_registered)
    {
        return ESP_OK;
    }

    esp_err_t cb_ret = app_task_register_event_callback(app_ctrl_on_task_event, NULL);
    if (cb_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "register ctrl task callback failed: %s", esp_err_to_name(cb_ret));
        return ESP_OK;
    }
    s_task_callback_registered = true;
    return ESP_OK;
}

// 所有依赖模块初始化完成后启动控制任务。
esp_err_t app_ctrl_start(void)
{
    if (!app_ctrl_runtime_is_initialized())
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
    return ESP_OK;
}
