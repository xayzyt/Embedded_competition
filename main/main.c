#include <stdbool.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "app_ai_capture.h"
#include "app_ch32_link.h"
#include "app_ctrl.h"
#include "app_dock_judge.h"
#include "app_drone_ai.h"
#include "app_main_services.h"
#include "app_preview_controller.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"
#include "bsp_display_port.h"

static const char *TAG = "main";

#define APP_TARGET_TAG_ID   (1U)
#define APP_TAG_SIZE_MM     (60)
#define APP_FOCAL_LENGTH_PX (314.0f)

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
    if (!app_ui_show_loading())
    {
        ESP_LOGE(TAG, "loading UI create failed");
        return false;
    }
    app_display_backlight_on();
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
    return cfg;
}

static void app_init_runtime_modules(const app_dock_judge_config_t *dock_cfg)
{
    app_ui_set_loading_progress(5);
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));
    app_ui_set_loading_progress(15);
    ESP_ERROR_CHECK(app_vision_init());
    app_ui_set_loading_progress(25);
    ESP_ERROR_CHECK(app_dock_judge_init(dock_cfg));
    app_ui_set_loading_progress(35);
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));
    app_ui_set_loading_progress(45);
    ESP_ERROR_CHECK(app_drone_ai_init());
    app_ui_set_loading_progress(55);
    ESP_ERROR_CHECK(app_ctrl_init());
    ESP_ERROR_CHECK(app_ctrl_start());
    app_ui_set_loading_progress(65);

    esp_err_t capture_ret = app_ai_capture_init();
    if (capture_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "AI capture disabled: %s", esp_err_to_name(capture_ret));
    }
}

static void app_show_ready_status(const app_dock_judge_config_t *dock_cfg)
{
    app_ui_set_status(dock_cfg->use_distance_gate ?
        "task: configured / dist gate on" :
        "task: configured / demo loose");
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_CONFIGURED);
}

void app_main(void)
{
    app_init_nvs();
    if (!app_start_display_ui())
    {
        return;
    }

    app_dock_judge_config_t dock_cfg = app_make_dock_config();
    app_init_runtime_modules(&dock_cfg);
    app_main_services_bind_ui_callbacks();

    app_ui_show_main_screen();
    app_ui_hide_loading();
    app_show_ready_status(&dock_cfg);

    // State-driven preview switching must be ready before cloud commands can start a task.
    (void)app_preview_controller_start();
    app_main_services_start();
}
