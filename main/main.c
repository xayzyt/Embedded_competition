/*
 * main.c - SkyAnchor ESP32-P4 应用入口
 *
 * 这个文件只负责启动编排：
 * NVS -> 显示/UI -> CH32 通信 -> 视觉/接驳规则
 * -> 任务/云端/控制 -> 摄像头/预览 -> 就绪状态。
 *
 * 具体功能细节放在各自模块里，例如 app_camera.c、
 * app_vision.c、app_ctrl.c、app_cloud.c、app_dock_judge.c。
 */

#include <stdbool.h>
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
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"
#include "bsp_display_port.h"

static const char *TAG = "main";

/* 接驳判定参数集中放在这里，后续调参时不用到处找。 */
#define APP_TARGET_TAG_ID            (1U)
#define APP_TAG_SIZE_MM              (100)
#define APP_DISTANCE_GATE_ENABLE     (1)
#define APP_FOCAL_LENGTH_PX          (314.0f)
#define APP_MIN_DISTANCE_MM          (260)
#define APP_MAX_DISTANCE_MM          (420)

static void app_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

static bool app_start_display_ui(void)
{
    if (!app_display_init()) {
        ESP_LOGE(TAG, "display init failed");
        return false;
    }

    if (!app_ui_create()) {
        ESP_LOGE(TAG, "ui create failed");
        return false;
    }

    app_ui_set_status("dock: booting");
    app_ui_set_vision_text("vision: init");
    app_ui_set_dock_text("dock dbg: init");
    return true;
}

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

static void app_init_runtime_modules(const app_dock_judge_config_t *dock_cfg)
{
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));
    ESP_ERROR_CHECK(app_vision_init());
    ESP_ERROR_CHECK(app_dock_judge_init(dock_cfg));
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));
    ESP_ERROR_CHECK(app_cloud_init());
    ESP_ERROR_CHECK(app_ctrl_init());
    ESP_ERROR_CHECK(app_ctrl_start());
    ESP_ERROR_CHECK(app_ai_capture_init());
}

static void app_start_camera_pipeline(void)
{
    ESP_ERROR_CHECK(app_camera_init());
    ESP_ERROR_CHECK(app_vision_start());
    ESP_ERROR_CHECK(app_camera_preview_start());
}

static void app_show_ready_status(const app_dock_judge_config_t *dock_cfg)
{
    char vision_text[64];

    if (dock_cfg->use_distance_gate) {
        app_ui_set_status("task: configured / dist gate on");
    } else {
        app_ui_set_status("task: configured / z calib");
    }

    snprintf(vision_text,
             sizeof(vision_text),
             "task:configured target:%u src:local",
             (unsigned)dock_cfg->target_tag_id);
    app_ui_set_vision_text(vision_text);
}

void app_main(void)
{
    ESP_LOGI(TAG, "==== SkyAnchor AprilTag + CH32 dock chain start ====");

    app_init_nvs();

    if (!app_start_display_ui()) {
        return;
    }

    app_dock_judge_config_t dock_cfg = app_make_dock_config();
    app_log_dock_config(&dock_cfg);

    app_init_runtime_modules(&dock_cfg);
    app_start_camera_pipeline();
    app_show_ready_status(&dock_cfg);

    ESP_LOGI(TAG, "system ready");
}
