#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
#define APP_TAG_SIZE_MM              (100)
#define APP_DISTANCE_GATE_ENABLE     (1)
#define APP_FOCAL_LENGTH_PX          (314.0f)
#define APP_MIN_DISTANCE_MM          (260)
#define APP_MAX_DISTANCE_MM          (420)

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
    if (!app_display_init())
    {
        ESP_LOGE(TAG, "display init failed");
        return false;
    }

    if (!app_ui_create())
    {
        ESP_LOGE(TAG, "ui create failed");
        return false;
    }

    /* 先绘制首帧再开背光，避免上电时屏幕先闪白。 */
    app_ui_show_loading("Display ready");
    app_display_backlight_on();
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

/* 初始化 CH32、视觉、接驳判定、任务、云端、控制和抓图模块。 */
static void app_init_runtime_modules(const app_dock_judge_config_t *dock_cfg)
{
    esp_err_t cloud_ret;

    /* 较慢的外设和服务启动期间保持加载页可见。 */
    app_ui_set_loading_text("Starting CH32 link");
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));
    app_ui_set_loading_text("Preparing vision");
    ESP_ERROR_CHECK(app_vision_init());
    app_ui_set_loading_text("Loading dock judge");
    ESP_ERROR_CHECK(app_dock_judge_init(dock_cfg));
    app_ui_set_loading_text("Loading task state");
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));
    app_ui_set_loading_text("Loading drone AI");
    ESP_ERROR_CHECK(app_drone_ai_init());

    app_ui_set_loading_text("Connecting cloud");
    cloud_ret = app_cloud_init();
    if (cloud_ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "cloud init failed (%s), continue with local vision/control",
                 esp_err_to_name(cloud_ret));
        app_ui_set_status("task: local mode / cloud offline");
    }

    app_ui_set_loading_text("Starting controller");
    ESP_ERROR_CHECK(app_ctrl_init());
    ESP_ERROR_CHECK(app_ctrl_start());
    app_ui_set_loading_text("Preparing capture");
    ESP_ERROR_CHECK(app_ai_capture_init());
}

/* 启动摄像头预览和视觉流，成功后隐藏启动加载页。 */
static void app_start_camera_pipeline(void)
{
    esp_err_t ai_ready_ret;

    /* 预览帧开始流动后，摄像头画布会替换启动加载层。 */
    app_ui_set_loading_text("Loading drone AI model");
    ai_ready_ret = app_drone_ai_wait_ready(8000);
    if (ai_ready_ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "drone AI model not ready yet (%s), start preview first",
                 esp_err_to_name(ai_ready_ret));
        app_ui_set_capture_text("ai: load timeout");
        app_ui_set_loading_text("Starting camera without AI");
    }
    app_ui_set_loading_text("Starting camera");
    ESP_ERROR_CHECK(app_camera_init());
    app_ui_set_loading_text("Starting vision stream");
    ESP_ERROR_CHECK(app_vision_start());
    app_ui_set_loading_text("Opening preview");
    ESP_ERROR_CHECK(app_camera_preview_start());
    app_ui_set_loading_text("Waiting camera frame");
    if (!app_camera_wait_first_frame(1500))
    {
        ESP_LOGW(TAG, "first camera frame did not reach LVGL before loading timeout");
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
        app_ui_set_status("task: configured / z calib");
    }

    snprintf(vision_text,
             sizeof(vision_text),
             "task:configured target:%u src:local",
             (unsigned)dock_cfg->target_tag_id);
    app_ui_set_vision_text(vision_text);
}

/* ESP-IDF 应用入口，按依赖顺序拉起整套接驳系统。 */
void app_main(void)
{
    app_init_nvs();

    if (!app_start_display_ui())
    {
        return;
    }

    app_dock_judge_config_t dock_cfg = app_make_dock_config();

    app_log_dock_config(&dock_cfg);
    app_init_runtime_modules(&dock_cfg);
    app_start_camera_pipeline();
    app_show_ready_status(&dock_cfg);

    ESP_LOGI(TAG, "system ready");
}
