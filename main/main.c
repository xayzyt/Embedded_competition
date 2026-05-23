#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
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

static const char *TAG = "main";

/* -------------------------------------------------------------------------- */
/* 接驳默认参数                                                            */
/* -------------------------------------------------------------------------- */

/* 接驳判定参数集中放在这里，后续调参时不用到处找。 */
#define APP_TARGET_TAG_ID            (1U)
#define APP_TAG_SIZE_MM              (60)
#define APP_DISTANCE_GATE_ENABLE     (0)
#define APP_FOCAL_LENGTH_PX          (314.0f)
#define APP_MIN_DISTANCE_MM          (120)
#define APP_MAX_DISTANCE_MM          (700)
#define APP_CLOUD_START_TASK_STACK   (8 * 1024)
#define APP_CLOUD_START_TASK_PRIO    4

/* -------------------------------------------------------------------------- */
/* 启动步骤                                                                  */
/* -------------------------------------------------------------------------- */

/* 初始化 NVS，遇到空间不足或版本变化时自动擦除重建。 */
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

/* 初始化显示和常驻 UI，并在开背光前先绘制加载页。 */
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

    /* 先绘制首帧再开背光，避免上电时屏幕先闪白。 */
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

/* 生成本项目使用的接驳判定配置，集中覆盖默认参数。 */
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

/* -------------------------------------------------------------------------- */
/* 运行时启动流程                                                             */
/* -------------------------------------------------------------------------- */

/* 打印关键接驳参数，方便从串口日志确认标定值。 */
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

/* 初始化 CH32、视觉、接驳判定、任务、控制和抓图模块。 */
static void app_init_runtime_modules(const app_dock_judge_config_t *dock_cfg)
{
    /* 较慢的外设和服务启动期间保持加载页可见。 */
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
    ESP_ERROR_CHECK(app_ai_capture_init());
}

/* 启动摄像头预览和视觉流，成功后隐藏启动加载页。 */
static void app_start_camera_pipeline(void)
{
    esp_err_t ai_ready_ret;

    /* 预览帧开始流动后，摄像头画布会替换启动加载层。 */
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
    ESP_ERROR_CHECK(app_camera_init());
    app_ui_set_loading_text("启动视觉流");
    app_ui_set_loading_progress(90);
    ESP_ERROR_CHECK(app_vision_start());
    app_ui_set_loading_text("开启预览");
    app_ui_set_loading_progress(95);
    ESP_ERROR_CHECK(app_camera_preview_start());
    app_ui_set_loading_text("等待摄像头帧");
    app_ui_set_loading_progress(100);
    if (!app_camera_wait_first_frame(5000))
    {
        ESP_LOGW(TAG, "first camera frame did not reach LVGL before loading timeout");
        app_ui_set_loading_text("等待摄像头信号");
        (void)app_camera_wait_first_frame(UINT32_MAX);
    }
    app_ui_hide_loading();
}

/* 系统启动完成后，在 UI 上显示初始任务状态。 */
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

    snprintf(vision_text,
             sizeof(vision_text),
             "task:configured target:%u src:local",
             (unsigned)dock_cfg->target_tag_id);
    app_ui_set_vision_text(vision_text);
}

/* -------------------------------------------------------------------------- */
/* 主界面状态刷新与任务事件回调                                                */
/* -------------------------------------------------------------------------- */

static bool s_camera_started = false;
static bool s_cloud_start_requested = false;

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
    }
    else
    {
        ESP_LOGI(TAG, "cloud async init done");
    }

    vTaskDelete(NULL);
}

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
    }
}

static void app_camera_start_task(void *arg)
{
    (void)arg;
    app_start_camera_pipeline();
    app_ui_set_status("task: active");
    vTaskDelete(NULL);
}

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
        app_ui_show_main_screen();
    }
}

static void app_on_task_event(app_task_event_t event,
                              const app_task_snapshot_t *snap,
                              void *user_ctx)
{
    (void)user_ctx;
    if (event != APP_TASK_EVENT_STATE_CHANGED || snap == NULL)
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

static void app_pickup_cb(void)
{
    esp_err_t ret = app_ch32_link_send_cmd_and_wait_ack('D', 2000);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "open inner door failed: %s", esp_err_to_name(ret));
        app_ui_main_screen_show_pickup(true);
        app_ui_main_screen_set_task_text("pickup failed / retry");
        return;
    }

    app_ui_main_screen_show_pickup(false);
    app_ui_main_screen_set_task_text("waiting task");
}

static void app_weather_sim_cb(void)
{
    app_cloud_simulate_severe_weather();
}

static void app_main_screen_status_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        bool wifi = app_cloud_is_wifi_connected();
        bool mqtt = app_cloud_is_mqtt_connected();
        bool ch32 = app_ch32_link_is_ready();
        app_ui_main_screen_update_status(wifi, mqtt, ch32);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ESP-IDF 应用入口，按依赖顺序拉起整套接驳系统。 */
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
    app_ui_show_main_screen();
    app_ui_hide_loading();
    app_show_ready_status(&dock_cfg);

    esp_err_t cb_ret = app_task_register_event_callback(app_on_task_event, NULL);
    if (cb_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "register task event callback failed: %s", esp_err_to_name(cb_ret));
        app_ui_set_status("task: event callback failed");
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
