#include "app_preview_controller.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_camera.h"
#include "app_drone_ai.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"

// 根据任务状态启动、暂停相机，并在主屏和预览画面之间切换。
static const char *TAG = "app_preview";

#define APP_CAMERA_START_TASK_STACK     (12 * 1024)
#define APP_CAMERA_START_TASK_PRIO      5
#define APP_TASK_EVENT_TASK_STACK       (8 * 1024)
#define APP_TASK_EVENT_TASK_PRIO        5
#define APP_PREVIEW_FIRST_FRAME_WAIT_MS 12000U
#define APP_PREVIEW_SWITCH_WAIT_MS      800U

static TaskHandle_t s_task_event_task = NULL;
static QueueHandle_t s_task_event_queue = NULL;
static bool s_camera_started = false;
static bool s_event_callback_registered = false;

// 相机启动失败时返回主屏并显示失败状态。
static void app_show_camera_failure(void)
{
    app_camera_pause();
    app_ui_hide_loading();
    app_ui_set_status("task: camera start failed");
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_CAMERA_FAILED);
    app_ui_show_main_screen();
}

// 暂停预览并返回主屏；相机资源保留供下一次任务复用。
static void app_leave_camera_preview(bool show_pickup)
{
    app_camera_pause();
    app_ui_show_main_screen();
    app_ui_main_screen_show_pickup(show_pickup);
}

// 判断当前任务阶段是否需要显示相机预览。
static bool app_task_wants_camera_preview(void)
{
    app_task_snapshot_t snap = {0};
    if (!app_task_peek_snapshot(&snap))
    {
        return true;
    }
    return snap.active &&
           (snap.state == APP_TASK_STATE_WAIT_APPROACH ||
            snap.state == APP_TASK_STATE_AUTH_PASSED ||
            snap.state == APP_TASK_STATE_DOCKING);
}

// 启动 AI、相机、视觉和预览；AI 超时不会阻止相机继续启动。
static esp_err_t app_start_camera_pipeline(void)
{
    app_ui_set_loading_progress(80);
    esp_err_t ai_ready_ret = app_drone_ai_wait_ready(8000);
    if (ai_ready_ret != ESP_OK)
    {
        ESP_LOGW(TAG,
            "drone AI model not ready yet (%s), start preview first",
            esp_err_to_name(ai_ready_ret));
    }

    app_ui_set_loading_progress(85);
    esp_err_t ret = app_camera_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    app_ui_set_loading_progress(90);
    ret = app_vision_start();
    if (ret != ESP_OK)
    {
        return ret;
    }

    app_ui_set_loading_progress(95);
    ret = app_camera_preview_start();
    if (ret != ESP_OK)
    {
        return ret;
    }

    app_ui_set_loading_progress(100);
    // 必须等到首帧送入 LVGL 后才能隐藏启动页。
    if (!app_camera_wait_first_frame(APP_PREVIEW_FIRST_FRAME_WAIT_MS))
    {
        ESP_LOGW(TAG, "first camera frame did not reach LVGL before loading timeout");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "camera preview first frame ready");
    app_ui_hide_loading();
    return ESP_OK;
}

static void app_camera_start_task(void *arg)
{
    (void)arg;
    esp_err_t ret = app_start_camera_pipeline();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "camera pipeline start failed: %s", esp_err_to_name(ret));
        s_camera_started = false;
        app_show_camera_failure();
        vTaskDelete(NULL);
        return;
    }

    app_ui_set_status("task: active");
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_ACTIVE);
    // 相机启动较慢，完成后重新确认任务是否仍需要预览。
    if (app_task_wants_camera_preview())
    {
        ESP_LOGI(TAG, "enter camera preview");
        app_ui_hide_main_screen();
    }
    vTaskDelete(NULL);
}

static void app_start_camera_if_needed(void)
{
    if (s_camera_started)
    {
        return;
    }

    // 提前置位，防止连续任务事件重复创建相机启动任务。
    s_camera_started = true;
    BaseType_t ok = xTaskCreate(app_camera_start_task,
        "cam_start",
        APP_CAMERA_START_TASK_STACK,
        NULL,
        APP_CAMERA_START_TASK_PRIO,
        NULL);
    if (ok != pdPASS)
    {
        s_camera_started = false;
        ESP_LOGE(TAG, "create camera start task failed");
        app_show_camera_failure();
    }
}

// 恢复预览并等待有效画面，避免切屏后短暂显示空白。
static void app_switch_to_camera_preview(void)
{
    const bool camera_was_started = s_camera_started;
    const uint32_t display_count = app_camera_display_count();
    app_camera_resume();
    app_start_camera_if_needed();
    if (!camera_was_started)
    {
        return;
    }

    if (app_camera_wait_display_count_after(display_count, APP_PREVIEW_SWITCH_WAIT_MS) ||
        display_count > 0U)
    {
        if (app_task_wants_camera_preview())
        {
            app_ui_hide_main_screen();
        }
        return;
    }

    ESP_LOGW(TAG, "camera preview frame delayed, keep main screen visible");
    app_ui_main_screen_set_task_text("camera frame delay");
}

static void app_handle_task_state_changed(const app_task_snapshot_t *snap)
{
    if (snap == NULL)
    {
        return;
    }

    // 任务开始进入预览，任务结束或异常时返回主屏。
    switch (snap->state) {
    case APP_TASK_STATE_WAIT_APPROACH:
        app_switch_to_camera_preview();
        break;
    case APP_TASK_STATE_COMPLETED:
        app_leave_camera_preview(true);
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_COMPLETED);
        break;
    case APP_TASK_STATE_FAULT:
    case APP_TASK_STATE_CANCELLED:
        app_leave_camera_preview(false);
        break;
    default:
        break;
    }
}

static void app_task_event_task(void *arg)
{
    (void)arg;
    app_task_snapshot_t snap = {0};
    for (;;)
    {
        if (xQueueReceive(s_task_event_queue, &snap, portMAX_DELAY) == pdPASS)
        {
            app_handle_task_state_changed(&snap);
        }
    }
}

// 创建任务状态处理队列和工作任务；队列只保留最新状态。
static esp_err_t app_start_task_event_worker(void)
{
    if (s_task_event_queue == NULL)
    {
        s_task_event_queue = xQueueCreate(1, sizeof(app_task_snapshot_t));
        if (s_task_event_queue == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_task_event_task != NULL)
    {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(app_task_event_task,
        "main_task_evt",
        APP_TASK_EVENT_TASK_STACK,
        NULL,
        APP_TASK_EVENT_TASK_PRIO,
        &s_task_event_task);
    if (ok != pdPASS)
    {
        s_task_event_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void app_on_task_event(app_task_event_t event,
    const app_task_snapshot_t *snap,
    void *user_ctx)
{
    (void)user_ctx;
    if (event != APP_TASK_EVENT_STATE_CHANGED ||
        snap == NULL ||
        s_task_event_queue == NULL)
    {
        return;
    }

    // 覆盖旧快照，使工作任务始终处理最新状态。
    (void)xQueueOverwrite(s_task_event_queue, snap);
}

esp_err_t app_preview_controller_start(void)
{
    // 先创建事件处理任务，再注册任务状态回调。
    esp_err_t ret = app_start_task_event_worker();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "start task event worker failed: %s", esp_err_to_name(ret));
        app_ui_set_status("task: event worker failed");
        return ret;
    }

    if (s_event_callback_registered)
    {
        return ESP_OK;
    }

    ret = app_task_register_event_callback(app_on_task_event, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "register task event callback failed: %s", esp_err_to_name(ret));
        app_ui_set_status("task: event callback failed");
        return ret;
    }

    s_event_callback_registered = true;
    return ESP_OK;
}
