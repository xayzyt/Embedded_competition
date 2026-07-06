#include "app_preview_controller.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_camera.h"
#include "app_drone_ai.h"
#include "app_safety_takeover.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"

// 根据任务状态启动、暂停相机，并在主屏和预览画面之间切换。
static const char *TAG = "app_preview";

#define APP_CAMERA_START_TASK_STACK     (12 * 1024)
#define APP_CAMERA_START_TASK_PRIO      5
#define APP_TASK_EVENT_TASK_STACK       (8 * 1024)
#define APP_TASK_EVENT_TASK_PRIO        5
#define APP_TASK_STATE_POLL_MS          250U
#define APP_PREVIEW_INTRO_MS            2000U
#define APP_PREVIEW_FIRST_FRAME_WAIT_MS 12000U
#define APP_PREVIEW_SWITCH_WAIT_MS      800U
#define APP_DRONE_AI_READY_WAIT_MS      200U

static TaskHandle_t s_task_event_task = NULL;
static QueueHandle_t s_task_event_queue = NULL;
static portMUX_TYPE s_preview_switch_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_camera_started = false;
static bool s_event_callback_registered = false;
static volatile bool s_preview_visible = false;
static bool s_preview_switching = false;
static app_task_state_t s_last_handled_state = APP_TASK_STATE_IDLE;
static bool s_have_last_handled_state = false;

// 相机启动失败时返回主屏并显示失败状态。
static void app_show_camera_failure(void)
{
    const bool safety_preview = app_safety_takeover_preview_active();
    app_camera_pause();
    app_ui_hide_task_intro();
    app_ui_set_preview_hud_visible(false);
    app_ui_hide_loading();
    app_ui_set_status("task: camera start failed");
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_CAMERA_FAILED);
    s_preview_visible = false;
    app_task_cancel("camera start failed");
    app_ui_show_main_screen();
    if (safety_preview)
    {
        app_safety_takeover_mark_failed();
    }
}

// 暂停预览并返回主屏；相机资源保留供下一次任务复用。
static bool app_leave_camera_preview(void)
{
    app_ui_hide_task_intro();
    app_ui_set_preview_hud_visible(false);
    app_ui_safety_takeover_set_visible(false);
    if (!app_ui_show_main_screen())
    {
        ESP_LOGW(TAG, "show main screen failed, will retry");
        return false;
    }
    app_camera_pause();
    s_preview_visible = false;
    return true;
}

// 只有活动中的识别、鉴权和机械执行阶段需要停留在相机预览。
static bool app_task_snapshot_wants_camera_preview(const app_task_snapshot_t *snap)
{
    if (app_safety_takeover_preview_active())
    {
        return true;
    }
    if (snap == NULL)
    {
        return true;
    }
    return snap->active &&
           (snap->state == APP_TASK_STATE_WAIT_APPROACH ||
            snap->state == APP_TASK_STATE_AUTH_PASSED ||
            snap->state == APP_TASK_STATE_DOCKING);
}

// 判断当前任务阶段是否需要显示相机预览。
static bool app_task_wants_camera_preview(void)
{
    app_task_snapshot_t snap = {0};
    if (!app_task_peek_snapshot(&snap))
    {
        return true;
    }
    return app_task_snapshot_wants_camera_preview(&snap);
}

// Guard the one-second intro against concurrent switch attempts.
static bool app_preview_switch_try_begin(void)
{
    bool started = false;
    taskENTER_CRITICAL(&s_preview_switch_mux);
    if (!s_preview_switching)
    {
        s_preview_switching = true;
        started = true;
    }
    taskEXIT_CRITICAL(&s_preview_switch_mux);
    return started;
}

static void app_preview_switch_finish(void)
{
    taskENTER_CRITICAL(&s_preview_switch_mux);
    s_preview_switching = false;
    taskEXIT_CRITICAL(&s_preview_switch_mux);
}

static bool app_enter_camera_preview(bool show_intro)
{
    if (!app_preview_switch_try_begin())
    {
        return false;
    }
    const bool safety_preview = app_safety_takeover_preview_active();

    app_task_snapshot_t snap = {0};
    uint16_t target_id = 0;
    if (app_task_peek_snapshot(&snap))
    {
        target_id = snap.target_id;
    }

    if (show_intro && !safety_preview && app_ui_show_task_intro(target_id))
    {
        vTaskDelay(pdMS_TO_TICKS(APP_PREVIEW_INTRO_MS));
    }

    bool entered = false;
    if (app_task_wants_camera_preview())
    {
        app_ui_hide_main_screen();
        if (safety_preview)
        {
            app_ui_set_preview_hud_visible(false);
            app_ui_safety_takeover_set_visible(true);
        }
        else
        {
            app_ui_set_preview_hud_visible(true);
        }
        s_preview_visible = true;
        entered = true;
    }
    if (!safety_preview)
    {
        app_ui_hide_task_intro();
    }
    app_preview_switch_finish();
    return entered;
}

static bool app_enter_camera_preview_with_intro(void)
{
    return app_enter_camera_preview(true);
}

static bool app_enter_camera_preview_without_intro(void)
{
    return app_enter_camera_preview(false);
}

// Request AI loading, then start camera preview without waiting for the model.
static esp_err_t app_start_camera_pipeline(void)
{
    app_ui_set_loading_progress(80);
    esp_err_t ai_ready_ret = app_drone_ai_wait_ready(APP_DRONE_AI_READY_WAIT_MS);
    if (ai_ready_ret != ESP_OK)
    {
        ESP_LOGW(TAG,
            "drone AI model still loading (%s), start camera preview now",
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
        (void)app_enter_camera_preview_with_intro();
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
static void app_switch_to_camera_preview(bool show_intro)
{
    const bool safety_preview = app_safety_takeover_preview_active();
    const bool camera_was_started = s_camera_started;
    const uint32_t display_count = app_camera_display_count();
    app_camera_resume();
    app_start_camera_if_needed();
    if (safety_preview)
    {
        if (app_task_wants_camera_preview())
        {
            (void)app_enter_camera_preview_without_intro();
        }
        return;
    }
    if (!camera_was_started)
    {
        return;
    }

    if (app_camera_wait_display_count_after(display_count, APP_PREVIEW_SWITCH_WAIT_MS) ||
        display_count > 0U)
    {
        if (app_task_wants_camera_preview())
        {
            if (show_intro)
            {
                (void)app_enter_camera_preview_with_intro();
            }
            else
            {
                (void)app_enter_camera_preview_without_intro();
            }
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

    const bool wants_preview = app_task_snapshot_wants_camera_preview(snap);
    const bool state_changed =
        !s_have_last_handled_state || s_last_handled_state != snap->state;
    const bool needs_visibility_fix =
        wants_preview ? !s_preview_visible : s_preview_visible;

    if (!state_changed && !needs_visibility_fix)
    {
        return;
    }

    // 任务开始进入预览，任务结束或异常时返回主屏；周期轮询负责补救丢失的事件。
    if (wants_preview)
    {
        if (needs_visibility_fix)
        {
            app_switch_to_camera_preview(snap->state == APP_TASK_STATE_WAIT_APPROACH);
        }
    }
    else
    {
        const bool completed = snap->state == APP_TASK_STATE_COMPLETED;
        const bool terminal =
            completed ||
            snap->state == APP_TASK_STATE_FAULT ||
            snap->state == APP_TASK_STATE_CANCELLED;
        if ((needs_visibility_fix || terminal) &&
            app_leave_camera_preview() &&
            completed)
        {
            app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_COMPLETED);
        }
    }

    s_last_handled_state = snap->state;
    s_have_last_handled_state = true;
}

static void app_task_event_task(void *arg)
{
    (void)arg;
    app_task_snapshot_t snap = {0};
    for (;;)
    {
        if (xQueueReceive(s_task_event_queue,
            &snap,
            pdMS_TO_TICKS(APP_TASK_STATE_POLL_MS)) != pdPASS)
        {
            if (!app_task_peek_snapshot(&snap))
            {
                continue;
            }
        }
        if (snap.inited)
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
