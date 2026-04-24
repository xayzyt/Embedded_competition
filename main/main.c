#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bsp_display_port.h"
#include "app_ui.h"
#include "app_camera.h"
#include "app_vision.h"
#include "app_dock_judge.h"
#include "app_ch32_link.h"
#include "app_ctrl.h"
#include "app_task.h"
#include "app_cloud.h"
static const char *TAG = "main";
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
void app_main(void)
{
    ESP_LOGI(TAG, "==== SkyAnchor AprilTag + CH32 dock chain start ====");
    app_init_nvs();
    if (!app_display_init()) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    if (!app_ui_create()) {
        ESP_LOGE(TAG, "ui create failed");
        return;
    }
    app_ui_set_status("dock: booting");
    app_ui_set_vision_text("vision: init");
    app_ui_set_dock_text("dock dbg: init");
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));
    ESP_ERROR_CHECK(app_vision_init());
    app_dock_judge_config_t dock_cfg;
    app_dock_judge_get_default_config(&dock_cfg);
    dock_cfg.use_target_id = true;
    dock_cfg.target_tag_id = APP_TARGET_TAG_ID;
    dock_cfg.tag_size_mm = APP_TAG_SIZE_MM;
    dock_cfg.focal_length_px = APP_FOCAL_LENGTH_PX;
    dock_cfg.use_distance_gate = (APP_DISTANCE_GATE_ENABLE != 0);
    dock_cfg.min_distance_mm = APP_MIN_DISTANCE_MM;
    dock_cfg.max_distance_mm = APP_MAX_DISTANCE_MM;
    ESP_LOGI(TAG,
             "dock cfg: target=%u tag=%ldmm focal=%.1f dist_gate=%d range=[%ld,%ld]",
             (unsigned)dock_cfg.target_tag_id,
             (long)dock_cfg.tag_size_mm,
             (double)dock_cfg.focal_length_px,
             dock_cfg.use_distance_gate,
             (long)dock_cfg.min_distance_mm,
             (long)dock_cfg.max_distance_mm);
    ESP_ERROR_CHECK(app_dock_judge_init(&dock_cfg));
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));
    ESP_ERROR_CHECK(app_cloud_init());
    ESP_ERROR_CHECK(app_ctrl_init());
    ESP_ERROR_CHECK(app_ctrl_start());
    ESP_ERROR_CHECK(app_camera_init());
    ESP_ERROR_CHECK(app_vision_start());
    ESP_ERROR_CHECK(app_camera_preview_start());
    if (dock_cfg.use_distance_gate) {
        app_ui_set_status("task: configured / dist gate on");
    } else {
        app_ui_set_status("task: configured / z calib");
    }
    app_ui_set_vision_text("task:configured target:1 src:local");
    ESP_LOGI(TAG, "system ready");
}
