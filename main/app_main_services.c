#include "app_main_services.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_audio_prompt.h"
#include "app_ch32_link.h"
#include "app_cloud.h"
#include "app_safety_takeover.h"
#include "app_ui.h"

// 处理主屏按钮，并启动云端和连接状态刷新任务。
static const char *TAG = "app_services";

#define APP_CLOUD_START_TASK_STACK       (8 * 1024)
#define APP_CLOUD_START_TASK_PRIO        4
#define APP_STATUS_TASK_STACK            2048
#define APP_STATUS_TASK_PRIO             3

static bool s_cloud_start_requested = false;
static bool s_audio_prompt_started = false;
static bool s_ready_prompt_requested = false;
static bool s_ready_prompt_error_logged = false;
static TaskHandle_t s_status_task = NULL;

static void app_show_main_state(app_ui_main_task_state_t state)
{
    app_ui_main_screen_set_task_state(state);
}

// 异步初始化云端；失败时系统继续使用本地功能。
static void app_cloud_start_task(void *arg)
{
    (void)arg;
    esp_err_t ret = app_cloud_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG,
            "cloud init failed (%s), continue with local vision/control",
            esp_err_to_name(ret));
        app_ui_set_status("task: local mode / cloud offline");
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_LOCAL_WAIT);
    }
    vTaskDelete(NULL);
}

static void app_start_cloud_async(void)
{
    if (s_cloud_start_requested)
    {
        return;
    }

    // 防止重复创建云端初始化任务。
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

static void app_start_audio_prompt_task(void)
{
    if (s_audio_prompt_started)
    {
        return;
    }

    esp_err_t ret = app_audio_prompt_init();
    if (ret == ESP_OK)
    {
        s_audio_prompt_started = true;
        app_ui_main_screen_set_voice_enabled(app_audio_prompt_is_enabled());
        return;
    }

    ESP_LOGW(TAG, "audio prompt disabled: %s", esp_err_to_name(ret));
    app_ui_main_screen_set_voice_enabled(app_audio_prompt_is_enabled());
}

static void app_voice_toggle_cb(void)
{
    const bool next_enabled = !app_audio_prompt_is_enabled();
    esp_err_t ret = app_audio_prompt_set_enabled(next_enabled, true);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "set voice enabled failed: %s", esp_err_to_name(ret));
    }
    app_ui_main_screen_set_voice_enabled(app_audio_prompt_is_enabled());
}

static void app_exception_demo_cb(void)
{
    esp_err_t ret = app_safety_takeover_start();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "start safety takeover failed: %s", esp_err_to_name(ret));
        app_ui_main_screen_set_task_text("安全保护启动失败");
    }
}

static void app_weather_sim_cb(void)
{
    esp_err_t ret = app_safety_takeover_trigger_typhoon();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "trigger safety typhoon failed: %s", esp_err_to_name(ret));
    }
}

static void app_exception_back_cb(void)
{
    app_show_main_state(APP_UI_MAIN_TASK_WAITING);
    (void)app_ui_show_main_screen();
}

// 每秒刷新主屏上的 Wi-Fi、MQTT 和 CH32 连接状态。
static void app_main_screen_status_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_cloud_status_t cloud = {0};
        app_cloud_get_status(&cloud);
        bool ch32_ready = app_ch32_link_is_ready();
        if (!ch32_ready)
        {
            ch32_ready = app_ch32_link_probe_ready(120) == ESP_OK;
        }
        app_ui_main_screen_update_status(
            cloud.wifi_connected,
            cloud.mqtt_connected,
            ch32_ready);
        if (!s_ready_prompt_requested && cloud.mqtt_connected && ch32_ready)
        {
            esp_err_t ret = app_audio_prompt_request_ready();
            if (ret == ESP_OK)
            {
                s_ready_prompt_requested = true;
                ESP_LOGI(TAG, "ready prompt requested after MQTT and CH32 ready");
            }
            else if (!s_ready_prompt_error_logged)
            {
                s_ready_prompt_error_logged = true;
                ESP_LOGW(TAG, "ready prompt request failed: %s", esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main_services_bind_ui_callbacks(void)
{
    app_ui_set_exception_demo_callback(app_exception_demo_cb);
    app_ui_set_voice_toggle_callback(app_voice_toggle_cb);
    app_ui_main_screen_set_voice_enabled(app_audio_prompt_is_enabled());
    app_ui_set_exception_back_callback(app_exception_back_cb);
    app_ui_set_weather_sim_callback(app_weather_sim_cb);
    app_ui_set_safety_typhoon_callback(app_weather_sim_cb);
}

void app_main_services_start(void)
{
    app_start_audio_prompt_task();
    app_start_cloud_async();
    if (s_status_task != NULL)
    {
        return;
    }

    BaseType_t ok = xTaskCreate(app_main_screen_status_task,
        "main_status",
        APP_STATUS_TASK_STACK,
        NULL,
        APP_STATUS_TASK_PRIO,
        &s_status_task);
    if (ok != pdPASS)
    {
        s_status_task = NULL;
        ESP_LOGE(TAG, "create main status task failed");
    }
}
