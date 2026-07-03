#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "app_ai_capture.h"
#include "app_ch32_link.h"
#include "app_ctrl.h"
#include "app_delivery_photo.h"
#include "app_dock_judge.h"
#include "app_drone_ai.h"
#include "app_main_services.h"
#include "app_preview_controller.h"
#include "app_task.h"
#include "app_ui.h"
#include "app_vision.h"
#include "bsp_display_port.h"

// 程序入口：按依赖顺序初始化显示、通信、视觉、控制和后台服务。
static const char *TAG = "main";

// 默认目标标签和测距标定参数。
#define APP_TARGET_TAG_ID   (1U)
#define APP_TAG_SIZE_MM     (60)
#define APP_FOCAL_LENGTH_PX (314.0f)
#define APP_TAG_CONFIRM_FRAMES (3U)

typedef struct {
    bool core_ready;
    bool services_ready;
    const char *status_text;
    app_ui_main_task_state_t ui_state;
} app_runtime_start_result_t;

// 初始化 NVS；分区格式不兼容或空间不足时擦除后重试。
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

// 初始化显示和启动页；UI 创建完成后再打开背光。
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

// 在默认参数上写入本机使用的标签和镜头标定值。
static app_dock_judge_config_t app_make_dock_config(void)
{
    app_dock_judge_config_t cfg;
    app_dock_judge_get_default_config(&cfg);
    cfg.use_target_id = true;
    cfg.target_tag_id = APP_TARGET_TAG_ID;
    cfg.tag_size_mm = APP_TAG_SIZE_MM;
    cfg.focal_length_px = APP_FOCAL_LENGTH_PX;
    cfg.min_stable_count = APP_TAG_CONFIRM_FRAMES;
    return cfg;
}

static bool app_record_start_step(const char *name,
    int32_t progress,
    esp_err_t ret,
    bool fatal)
{
    app_ui_set_loading_progress(progress);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "%s ready", name);
        return true;
    }

    if (fatal)
    {
        ESP_LOGE(TAG, "%s failed: %s", name, esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGW(TAG, "%s disabled: %s", name, esp_err_to_name(ret));
    }
    return false;
}

static void app_mark_start_failure(app_runtime_start_result_t *result,
    const char *status_text,
    app_ui_main_task_state_t ui_state,
    bool core_failed)
{
    result->status_text = status_text;
    result->ui_state = ui_state;
    if (core_failed)
    {
        result->core_ready = false;
    }
    result->services_ready = false;
    app_ui_set_status(status_text);
    app_ui_main_screen_set_task_state(ui_state);
}

// 按依赖顺序初始化通信、视觉、任务、AI 和控制模块。
static app_runtime_start_result_t app_init_runtime_modules(const app_dock_judge_config_t *dock_cfg)
{
    app_runtime_start_result_t result = {
        .core_ready = true,
        .services_ready = true,
        .status_text = dock_cfg->use_distance_gate ?
            "task: configured / dist gate on" :
            "task: configured / demo loose",
        .ui_state = APP_UI_MAIN_TASK_CONFIGURED,
    };

    app_ui_set_loading_progress(5);
    const bool ch32_ok = app_record_start_step("ch32 link",
        15,
        app_ch32_link_init(app_ctrl_on_ch32_line, NULL),
        false);

    if (!app_record_start_step("vision",
        25,
        app_vision_init(),
        true))
    {
        app_mark_start_failure(&result,
            "task: camera failed / vision init",
            APP_UI_MAIN_TASK_CAMERA_FAILED,
            true);
        return result;
    }

    if (!app_record_start_step("dock judge",
        35,
        app_dock_judge_init(dock_cfg),
        true))
    {
        app_mark_start_failure(&result,
            "task: local mode / dock init failed",
            APP_UI_MAIN_TASK_LOCAL_WAIT,
            true);
        return result;
    }

    if (!app_record_start_step("task state",
        45,
        app_task_init(APP_TARGET_TAG_ID),
        true))
    {
        app_mark_start_failure(&result,
            "task: local mode / task init failed",
            APP_UI_MAIN_TASK_LOCAL_WAIT,
            true);
        return result;
    }

    if (!app_record_start_step("drone ai",
        55,
        app_drone_ai_init(),
        false))
    {
        result.status_text = "task: configured / ai init failed";
        result.ui_state = APP_UI_MAIN_TASK_CONFIGURED;
        app_ui_set_status(result.status_text);
        app_ui_main_screen_set_task_state(result.ui_state);
    }

    if (!ch32_ok)
    {
        app_mark_start_failure(&result,
            "task: local mode / ch32 init failed",
            APP_UI_MAIN_TASK_LOCAL_WAIT,
            false);
        app_ui_set_loading_progress(65);
        return result;
    }

    if (!app_record_start_step("control init",
        60,
        app_ctrl_init(),
        false))
    {
        app_mark_start_failure(&result,
            "task: local mode / ctrl init failed",
            APP_UI_MAIN_TASK_LOCAL_WAIT,
            false);
        app_ui_set_loading_progress(65);
        return result;
    }

    if (!app_record_start_step("control task",
        65,
        app_ctrl_start(),
        false))
    {
        app_mark_start_failure(&result,
            "task: local mode / ctrl start failed",
            APP_UI_MAIN_TASK_LOCAL_WAIT,
            false);
        return result;
    }

    // 抓图是辅助功能，初始化失败时只关闭抓图，不中止系统启动。
    esp_err_t capture_ret = app_ai_capture_init();
    if (capture_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "AI capture disabled: %s", esp_err_to_name(capture_ret));
    }
    esp_err_t photo_ret = app_delivery_photo_init();
    if (photo_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "delivery photo disabled: %s", esp_err_to_name(photo_ret));
    }
    return result;
}

// 在主屏显示系统就绪状态和当前距离限制模式。
static void app_show_runtime_status(const app_runtime_start_result_t *result)
{
    app_ui_set_status(result->status_text);
    app_ui_main_screen_set_task_state(result->ui_state);
}

void app_main(void)
{
    app_init_nvs();
    if (!app_start_display_ui())
    {
        return;
    }

    app_dock_judge_config_t dock_cfg = app_make_dock_config();
    app_runtime_start_result_t runtime = app_init_runtime_modules(&dock_cfg);

    app_ui_show_main_screen();
    app_ui_hide_loading();
    app_show_runtime_status(&runtime);
    if (!runtime.core_ready || !runtime.services_ready)
    {
        ESP_LOGW(TAG, "startup degraded, background services not started");
        return;
    }

    app_main_services_bind_ui_callbacks();

    // 先监听任务状态，再启动云端，避免云端任务先到而预览尚未准备切换。
    esp_err_t preview_ret = app_preview_controller_start();
    if (preview_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "preview controller disabled: %s", esp_err_to_name(preview_ret));
        app_ui_set_status("task: local mode / event worker failed");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_LOCAL_WAIT);
        return;
    }
    app_main_services_start();
}
