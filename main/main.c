#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "app_ai_capture.h"
#include "app_camera.h"
#include "app_ch32_link.h"
#include "app_cloud.h"
#include "app_ctrl.h"
#include "app_dock_judge.h"
#include "app_drone_ai.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"
#include "bsp_display_port.h"

// 应用入口：负责串起显示、视觉、AI、云端、CH32 控制和任务状态机。
static const char *TAG = "main";

// 演示默认参数集中放在入口处，方便按一次启动流程阅读。
#define APP_TARGET_TAG_ID            (1U)
#define APP_TAG_SIZE_MM              (60)
#define APP_DISTANCE_GATE_ENABLE     (0)
#define APP_FOCAL_LENGTH_PX          (314.0f)
#define APP_MIN_DISTANCE_MM          (120)
#define APP_MAX_DISTANCE_MM          (700)
#define APP_CLOUD_START_TASK_STACK   (8 * 1024)
#define APP_CLOUD_START_TASK_PRIO    4
#define APP_TASK_EVENT_QUEUE_LEN      8
#define APP_TASK_EVENT_TASK_STACK     (8 * 1024)
#define APP_TASK_EVENT_TASK_PRIO      5
#define APP_WEATHER_EMERGENCY_TASK_STACK (12 * 1024)
#define APP_WEATHER_EMERGENCY_TASK_PRIO  5

// 任务状态回调可能来自 MQTT 或控制任务，UI 切换统一交给本地 worker 处理。
typedef struct {
    app_task_event_t event;
    app_task_snapshot_t snap;
} app_main_task_event_msg_t;
static TaskHandle_t s_weather_emergency_task = NULL;
static TaskHandle_t s_task_event_task = NULL;
static QueueHandle_t s_task_event_queue = NULL;
static portMUX_TYPE s_weather_emergency_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_weather_emergency_running = false;

// 初始化 NVS，遇到分区版本变化或空间不足时自动擦除后重试。
static void app_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// 启动显示屏和基础 UI；失败时保留日志并让 app_main 提前返回。
static bool app_start_display_ui(void)
{
    ESP_LOGI(TAG, "display step: init begin");
    if (!app_display_init())
    {
        ESP_LOGE(TAG, "display init failed");
        return false;
    }
    ESP_LOGI(TAG, "display step: init done");
    ESP_LOGI(TAG, "display step: ui create begin");
    if (!app_ui_create())
    {
        ESP_LOGE(TAG, "ui create failed");
        return false;
    }
    ESP_LOGI(TAG, "display step: ui create done");
    ESP_LOGI(TAG, "display step: loading begin");
    if (!app_ui_show_loading("显示屏就绪"))
    {
        ESP_LOGE(TAG, "loading UI create failed");
        return false;
    }
    ESP_LOGI(TAG, "display step: loading done, backlight on");
    app_display_backlight_on();
    ESP_LOGI(TAG, "display step: backlight done");
    app_ui_set_status("dock: booting");
    app_ui_set_vision_text("vision: init");
    app_ui_set_dock_text("dock dbg: init");
    return true;
}

// 生成本次演示使用的对接判定参数。
static app_dock_judge_config_t app_make_dock_config(void)
{
    app_dock_judge_config_t cfg;
    app_dock_judge_get_default_config(&cfg);
    cfg.use_target_id = true;
    cfg.target_tag_id = APP_TARGET_TAG_ID;
    cfg.tag_size_mm = APP_TAG_SIZE_MM;
    cfg.focal_length_px = APP_FOCAL_LENGTH_PX;
    cfg.use_distance_gate = (APP_DISTANCE_GATE_ENABLE != 0);
    cfg.min_distance_mm = APP_MIN_DISTANCE_MM;
    cfg.max_distance_mm = APP_MAX_DISTANCE_MM;
    return cfg;
}

// 打印对接判定关键参数，方便串口日志和现场调试对齐配置。
static void app_log_dock_config(const app_dock_judge_config_t *cfg)
{
    ESP_LOGI(TAG,
             "dock cfg: target=%u tag=%ldmm focal=%.1f dist_gate=%d range=[%ld,%ld]",
             (unsigned)cfg->target_tag_id,
             (long)cfg->tag_size_mm,
             (double)cfg->focal_length_px,
             (int)cfg->use_distance_gate,
             (long)cfg->min_distance_mm,
             (long)cfg->max_distance_mm);
}

// 按启动页进度逐个初始化运行期模块。
static void app_init_runtime_modules(const app_dock_judge_config_t *dock_cfg)
{
    app_ui_set_loading_text("启动 CH32 通信");
    app_ui_set_loading_progress(5);
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));
    app_ui_set_loading_text("准备视觉模块");
    app_ui_set_loading_progress(15);
    ESP_ERROR_CHECK(app_vision_init());
    app_ui_set_loading_text("加载对接判定");
    app_ui_set_loading_progress(25);
    ESP_ERROR_CHECK(app_dock_judge_init(dock_cfg));
    app_ui_set_loading_text("加载任务状态");
    app_ui_set_loading_progress(35);
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));
    app_ui_set_loading_text("加载无人机 AI");
    app_ui_set_loading_progress(45);
    ESP_ERROR_CHECK(app_drone_ai_init());
    app_ui_set_loading_text("启动控制器");
    app_ui_set_loading_progress(55);
    ESP_ERROR_CHECK(app_ctrl_init());
    ESP_ERROR_CHECK(app_ctrl_start());
    app_ui_set_loading_text("准备抓图模块");
    app_ui_set_loading_progress(65);
    esp_err_t capture_ret = app_ai_capture_init();
    if (capture_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "AI capture disabled: %s", esp_err_to_name(capture_ret));
        app_ui_set_capture_text("cap: disabled");
    }
}

// 启动相机预览链路：先等 AI 模型，随后开启摄像头、视觉检测和 LVGL 预览。
static esp_err_t app_start_camera_pipeline(void)
{
    esp_err_t ai_ready_ret;
    app_ui_set_loading_text("加载无人机 AI 模型");
    app_ui_set_loading_progress(80);
    ai_ready_ret = app_drone_ai_wait_ready(8000);
    if (ai_ready_ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "drone AI model not ready yet (%s), start preview first",
                 esp_err_to_name(ai_ready_ret));
        app_ui_set_capture_text("ai: load timeout");
        app_ui_set_loading_text("启动摄像头 (AI未就绪)");
    }
    app_ui_set_loading_text("启动摄像头");
    app_ui_set_loading_progress(85);
    esp_err_t ret = app_camera_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    app_ui_set_loading_text("启动视觉流");
    app_ui_set_loading_progress(90);
    ret = app_vision_start();
    if (ret != ESP_OK)
    {
        return ret;
    }
    app_ui_set_loading_text("开启预览");
    app_ui_set_loading_progress(95);
    ret = app_camera_preview_start();
    if (ret != ESP_OK)
    {
        return ret;
    }
    app_ui_set_loading_text("等待摄像头帧");
    app_ui_set_loading_progress(100);
    if (!app_camera_wait_first_frame(5000))
    {
        ESP_LOGW(TAG, "first camera frame did not reach LVGL before loading timeout");
        app_ui_set_loading_text("等待摄像头信号");
        return ESP_ERR_TIMEOUT;
    }
    app_ui_hide_loading();
    return ESP_OK;
}

// 主屏进入“已配置”状态，提示当前目标码和距离门控配置。
static void app_show_ready_status(const app_dock_judge_config_t *dock_cfg)
{
    char vision_text[64];
    if (dock_cfg->use_distance_gate)
    {
        app_ui_set_status("task: configured / dist gate on");
    } else
    {
        app_ui_set_status("task: configured / demo loose");
    }
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_CONFIGURED);
    snprintf(vision_text,
             sizeof(vision_text),
             "task:configured target:%u src:local",
             (unsigned)dock_cfg->target_tag_id);
    app_ui_set_vision_text(vision_text);
}
static bool s_camera_started = false;
static bool s_cloud_start_requested = false;

// 云端启动会等待 ESP-Hosted 和 Wi-Fi，放到后台避免阻塞主屏。
static void app_cloud_start_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "cloud async init begin");
    esp_err_t ret = app_cloud_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "cloud init failed (%s), continue with local vision/control",
                 esp_err_to_name(ret));
        app_ui_set_status("task: local mode / cloud offline");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_LOCAL_WAIT);
    }
    else
    {
        ESP_LOGI(TAG, "cloud async init done");
    }
    vTaskDelete(NULL);
}

// 只提交一次云端后台初始化任务，避免 Wi-Fi/MQTT 重复启动。
static void app_start_cloud_async(void)
{
    if (s_cloud_start_requested)
    {
        return;
    }
    s_cloud_start_requested = true;
    BaseType_t ok = xTaskCreate(app_cloud_start_task,
        "cloud_start",
        APP_CLOUD_START_TASK_STACK,
        NULL,
        APP_CLOUD_START_TASK_PRIO,
        NULL);
    if (ok != pdPASS)
    {
        s_cloud_start_requested = false;
        ESP_LOGE(TAG, "create cloud start task failed");
        app_ui_set_status("task: local mode / cloud start failed");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_LOCAL_WAIT);
    }
}

// 相机链路的后台任务入口，失败时回退到主屏错误态。
static void app_camera_start_task(void *arg)
{
    (void)arg;
    esp_err_t ret = app_start_camera_pipeline();
    if (ret == ESP_OK)
    {
        app_ui_set_status("task: active");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_ACTIVE);
    }
    else
    {
        ESP_LOGE(TAG, "camera pipeline start failed: %s", esp_err_to_name(ret));
        s_camera_started = false;
        app_camera_pause();
        app_ui_hide_loading();
        app_ui_set_status("task: camera start failed");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_CAMERA_FAILED);
        app_ui_show_main_screen();
    }
    vTaskDelete(NULL);
}

// 摄像头资源较重，只有任务进入视觉阶段时才启动。
static void app_start_camera_if_needed(void)
{
    if (s_camera_started)
    {
        return;
    }
    s_camera_started = true;
    BaseType_t ok = xTaskCreate(app_camera_start_task, "cam_start", 4096, NULL, 5, NULL);
    if (ok != pdPASS)
    {
        s_camera_started = false;
        ESP_LOGE(TAG, "create camera start task failed");
        app_ui_set_status("task: camera start failed");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_CAMERA_FAILED);
        app_ui_show_main_screen();
    }
}

// 根据任务状态切换主屏和相机预览。
static void app_handle_task_state_changed(const app_task_snapshot_t *snap)
{
    if (snap == NULL)
    {
        return;
    }
    switch (snap->state) {
    case APP_TASK_STATE_WAIT_APPROACH:
        app_camera_resume();
        app_ui_hide_main_screen();
        app_start_camera_if_needed();
        break;
    case APP_TASK_STATE_COMPLETED:
        app_camera_pause();
        app_ui_show_main_screen();
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_COMPLETED);
        app_ui_main_screen_show_pickup(true);
        break;
    case APP_TASK_STATE_FAULT:
    case APP_TASK_STATE_CANCELLED:
        app_camera_pause();
        app_ui_show_main_screen();
        app_ui_main_screen_show_pickup(false);
        break;
    default:
        break;
    }
}

// 任务事件 worker：把任务模块回调转换成主线程侧的 UI/相机动作。
static void app_task_event_task(void *arg)
{
    (void)arg;
    app_main_task_event_msg_t msg = {0};
    for (;;)
    {
        if (xQueueReceive(s_task_event_queue, &msg, portMAX_DELAY) != pdPASS)
        {
            continue;
        }
        if (msg.event == APP_TASK_EVENT_STATE_CHANGED)
        {
            app_handle_task_state_changed(&msg.snap);
        }
    }
}

// 创建任务事件队列和 worker。
static esp_err_t app_start_task_event_worker(void)
{
    if (s_task_event_queue == NULL)
    {
        s_task_event_queue = xQueueCreate(APP_TASK_EVENT_QUEUE_LEN, sizeof(app_main_task_event_msg_t));
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

// 任务模块回调入口；队列满时用最新状态覆盖最旧状态。
static void app_on_task_event(app_task_event_t event,
                              const app_task_snapshot_t *snap,
                              void *user_ctx)
{
    (void)user_ctx;
    if (snap == NULL || s_task_event_queue == NULL)
    {
        return;
    }
    app_main_task_event_msg_t msg = {
        .event = event,
        .snap = *snap,
    };
    // 队列满时丢弃旧状态，主屏只需要按最新快照收敛。
    if (xQueueSend(s_task_event_queue, &msg, 0) != pdPASS)
    {
        app_main_task_event_msg_t dropped = {0};
        (void)xQueueReceive(s_task_event_queue, &dropped, 0);
        (void)xQueueSend(s_task_event_queue, &msg, 0);
    }
}

// 用户取货按钮回调，确认任务完成且天气允许后打开内舱门。
static void app_pickup_cb(void)
{
    if (app_cloud_is_weather_docking_blocked())
    {
        app_ui_main_screen_show_pickup(false);
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
        return;
    }
    app_task_snapshot_t snap = {0};
    if (!app_task_peek_snapshot(&snap) || snap.state != APP_TASK_STATE_COMPLETED)
    {
        app_ui_main_screen_show_pickup(false);
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_PICKUP_FAILED);
        return;
    }
    esp_err_t ret = app_ch32_link_send_proto_cmd_and_wait_ack(APP_CH32_PROTO_CMD_OPEN_INNER_DOOR, 2000);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "open inner door failed: %s", esp_err_to_name(ret));
        app_ui_main_screen_show_pickup(true);
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_PICKUP_FAILED);
        return;
    }
    app_ui_main_screen_show_pickup(false);
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WAITING);
}

// 切换天气模拟状态，用于现场演示天气保护流程。
static void app_weather_sim_cb(void)
{
    app_cloud_set_weather_simulated(!app_cloud_is_weather_simulated());
}

// 天气紧急保护可能被 UI 和云端共同触发，需要串行化。
static bool app_weather_emergency_try_begin(void)
{
    bool ok = false;
    taskENTER_CRITICAL(&s_weather_emergency_mux);
    if (!s_weather_emergency_running)
    {
        s_weather_emergency_running = true;
        ok = true;
    }
    taskEXIT_CRITICAL(&s_weather_emergency_mux);
    return ok;
}

// 天气保护任务结束时释放运行标记。
static void app_weather_emergency_finish(void)
{
    taskENTER_CRITICAL(&s_weather_emergency_mux);
    s_weather_emergency_running = false;
    s_weather_emergency_task = NULL;
    taskEXIT_CRITICAL(&s_weather_emergency_mux);
}

// 后台触发天气紧急保护，避免 UI 回调里做较重的云端/任务处理。
static void app_weather_emergency_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "weather emergency guard begin");
    app_cloud_trigger_weather_emergency();
    ESP_LOGI(TAG,
        "weather emergency guard done, stack_free=%u",
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    app_weather_emergency_finish();
    vTaskDelete(NULL);
}

// 用户手动触发恶劣天气保护时，立即暂停相机并切回主屏。
static void app_weather_emergency_cb(void)
{
    app_camera_pause();
    app_ui_show_main_screen();
    app_ui_main_screen_show_pickup(false);
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
    if (app_cloud_is_weather_docking_blocked())
    {
        return;
    }
    if (!app_weather_emergency_try_begin())
    {
        return;
    }
    BaseType_t ok = xTaskCreate(app_weather_emergency_task,
        "weather_guard",
        APP_WEATHER_EMERGENCY_TASK_STACK,
        NULL,
        APP_WEATHER_EMERGENCY_TASK_PRIO,
        &s_weather_emergency_task);
    if (ok != pdPASS)
    {
        app_weather_emergency_finish();
        ESP_LOGE(TAG, "create weather emergency task failed");
        app_cloud_set_weather_simulated(true);
    }
}

// 主屏状态灯定时刷新，展示 Wi-Fi、MQTT 和 CH32 在线状态。
static void app_main_screen_status_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_cloud_status_t cloud = {0};
        app_cloud_get_status(&cloud);
        bool ch32 = app_ch32_link_is_ready();
        app_ui_main_screen_update_status(cloud.wifi_connected, cloud.mqtt_connected, ch32);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ESP-IDF 应用入口。
void app_main(void)
{
    ESP_LOGI(TAG, "app_main enter");
    ESP_LOGI(TAG, "nvs init begin");
    app_init_nvs();
    ESP_LOGI(TAG, "nvs init done");
    if (!app_start_display_ui())
    {
        return;
    }
    app_dock_judge_config_t dock_cfg = app_make_dock_config();
    app_log_dock_config(&dock_cfg);
    app_init_runtime_modules(&dock_cfg);
    app_ui_set_pickup_callback(app_pickup_cb);
    app_ui_set_weather_sim_callback(app_weather_sim_cb);
    app_ui_set_weather_emergency_callback(app_weather_emergency_cb);
    app_ui_show_main_screen();
    app_ui_hide_loading();
    app_show_ready_status(&dock_cfg);

    // UI 可见后再注册任务事件，并通过 main_task_evt 隔离调用栈。
    esp_err_t evt_ret = app_start_task_event_worker();
    if (evt_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "start task event worker failed: %s", esp_err_to_name(evt_ret));
        app_ui_set_status("task: event worker failed");
    }
    else
    {
        esp_err_t cb_ret = app_task_register_event_callback(app_on_task_event, NULL);
        if (cb_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "register task event callback failed: %s", esp_err_to_name(cb_ret));
            app_ui_set_status("task: event callback failed");
        }
    }
    app_start_cloud_async();
    BaseType_t status_task_ok = xTaskCreate(app_main_screen_status_task,
        "main_status",
        2048,
        NULL,
        3,
        NULL);
    if (status_task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "create main status task failed");
    }
    ESP_LOGI(TAG, "system ready - main screen displayed");
}
