#include "app_main_services.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_camera.h"
#include "app_ch32_link.h"
#include "app_cloud.h"
#include "app_task.h"
#include "app_ui.h"

// 处理主屏按钮，并启动云端和连接状态刷新任务。
static const char *TAG = "app_services";

#define APP_CLOUD_START_TASK_STACK       (8 * 1024)
#define APP_CLOUD_START_TASK_PRIO        4
#define APP_STATUS_TASK_STACK            2048
#define APP_STATUS_TASK_PRIO             3
#define APP_WEATHER_EMERGENCY_TASK_STACK (12 * 1024)
#define APP_WEATHER_EMERGENCY_TASK_PRIO  5
#define APP_EXCEPTION_DEMO_TASK_STACK    (4 * 1024)
#define APP_EXCEPTION_DEMO_TASK_PRIO     5

static portMUX_TYPE s_weather_emergency_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_exception_demo_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_weather_emergency_running = false;
static bool s_exception_demo_running = false;
static bool s_cloud_start_requested = false;
static uint8_t s_exception_weather_arg = 0;
static TaskHandle_t s_status_task = NULL;

static bool app_weather_emergency_try_begin(void);
static void app_weather_emergency_finish(void);
static void app_weather_emergency_task(void *arg);

static void app_show_main_state(app_ui_main_task_state_t state, bool show_pickup)
{
    app_ui_main_screen_show_pickup(show_pickup);
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

// 处理取货按钮：检查天气和任务状态后请求 CH32 打开内门。
static void app_pickup_cb(void)
{
    if (app_cloud_is_weather_docking_blocked())
    {
        app_show_main_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED, false);
        return;
    }

    app_task_snapshot_t snap = {0};
    if (!app_task_peek_snapshot(&snap) || snap.state != APP_TASK_STATE_COMPLETED)
    {
        app_show_main_state(APP_UI_MAIN_TASK_PICKUP_FAILED, false);
        return;
    }

    // 开门命令必须收到 CH32 确认，失败时保留取货按钮供用户重试。
    esp_err_t ret = app_ch32_link_send_proto_cmd_and_wait_ack(
        APP_CH32_PROTO_CMD_OPEN_INNER_DOOR,
        2000);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "open inner door failed: %s", esp_err_to_name(ret));
        app_show_main_state(APP_UI_MAIN_TASK_PICKUP_FAILED, true);
        return;
    }

    app_show_main_state(APP_UI_MAIN_TASK_WAITING, false);
}

static bool app_exception_demo_try_begin(void)
{
    bool started = false;
    taskENTER_CRITICAL(&s_exception_demo_mux);
    if (!s_exception_demo_running)
    {
        s_exception_demo_running = true;
        started = true;
    }
    taskEXIT_CRITICAL(&s_exception_demo_mux);
    return started;
}

static void app_exception_demo_finish(void)
{
    taskENTER_CRITICAL(&s_exception_demo_mux);
    s_exception_demo_running = false;
    taskEXIT_CRITICAL(&s_exception_demo_mux);
}

static void app_exception_demo_task(void *arg)
{
    (void)arg;
    app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_STARTING);
    if (!app_ch32_link_is_ready())
    {
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_CH32_OFFLINE);
        app_exception_demo_finish();
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = app_ch32_link_send_proto_cmd_and_wait_ack(
        APP_CH32_PROTO_CMD_START_DOCK,
        2000);
    if (ret == ESP_OK)
    {
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_RUNNING);
    }
    else
    {
        ESP_LOGW(TAG, "exception demo START_DOCK failed: %s", esp_err_to_name(ret));
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_FAILED);
    }
    app_exception_demo_finish();
    vTaskDelete(NULL);
}

static void app_exception_demo_cb(void)
{
    app_camera_pause();
    app_ui_main_screen_show_pickup(false);
    app_ui_main_screen_set_task_text("异常演示");
    if (!app_ui_show_exception_demo_screen())
    {
        return;
    }
    if (!app_exception_demo_try_begin())
    {
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_RUNNING);
        return;
    }
    BaseType_t ok = xTaskCreate(app_exception_demo_task,
        "exception_demo",
        APP_EXCEPTION_DEMO_TASK_STACK,
        NULL,
        APP_EXCEPTION_DEMO_TASK_PRIO,
        NULL);
    if (ok != pdPASS)
    {
        app_exception_demo_finish();
        ESP_LOGE(TAG, "create exception demo task failed");
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_FAILED);
    }
}

static void app_weather_sim_cb(void)
{
    app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_WEATHER);
    if (!app_weather_emergency_try_begin())
    {
        return;
    }
    BaseType_t ok = xTaskCreate(app_weather_emergency_task,
        "exception_guard",
        APP_WEATHER_EMERGENCY_TASK_STACK,
        &s_exception_weather_arg,
        APP_WEATHER_EMERGENCY_TASK_PRIO,
        NULL);
    if (ok != pdPASS)
    {
        app_weather_emergency_finish();
        ESP_LOGE(TAG, "create exception weather task failed");
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_FAILED);
    }
}

static void app_exception_back_cb(void)
{
    app_show_main_state(APP_UI_MAIN_TASK_WAITING, false);
    (void)app_ui_show_main_screen();
}

// 设置天气保护运行标志，防止连续点击重复创建任务。
static bool app_weather_emergency_try_begin(void)
{
    bool started = false;
    taskENTER_CRITICAL(&s_weather_emergency_mux);
    if (!s_weather_emergency_running)
    {
        s_weather_emergency_running = true;
        started = true;
    }
    taskEXIT_CRITICAL(&s_weather_emergency_mux);
    return started;
}

static void app_weather_emergency_finish(void)
{
    taskENTER_CRITICAL(&s_weather_emergency_mux);
    s_weather_emergency_running = false;
    taskEXIT_CRITICAL(&s_weather_emergency_mux);
}

// 在独立任务中执行天气保护，避免阻塞 LVGL 按钮回调。
static void app_weather_emergency_task(void *arg)
{
    const bool exception_demo = (arg == &s_exception_weather_arg);
    esp_err_t ret = exception_demo ?
        app_cloud_trigger_weather_demo_protection_wait() :
        app_cloud_trigger_weather_emergency_wait();
    if (ret != ESP_OK)
    {
        app_ui_exception_demo_set_state(APP_UI_EXCEPTION_DEMO_FAILED);
    }
    app_weather_emergency_finish();
    vTaskDelete(NULL);
}

// 立即停止预览并显示告警，然后启动后台保护任务。
static void app_weather_emergency_cb(void)
{
    app_camera_pause();
    app_ui_show_main_screen();
    app_show_main_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED, false);
    if (app_cloud_is_weather_docking_blocked() || !app_weather_emergency_try_begin())
    {
        return;
    }

    BaseType_t ok = xTaskCreate(app_weather_emergency_task,
        "weather_guard",
        APP_WEATHER_EMERGENCY_TASK_STACK,
        NULL,
        APP_WEATHER_EMERGENCY_TASK_PRIO,
        NULL);
    if (ok != pdPASS)
    {
        app_weather_emergency_finish();
        ESP_LOGE(TAG, "create weather emergency task failed");
        app_cloud_set_weather_simulated(true);
    }
}

// 每秒刷新主屏上的 Wi-Fi、MQTT 和 CH32 连接状态。
static void app_main_screen_status_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_cloud_status_t cloud = {0};
        app_cloud_get_status(&cloud);
        app_ui_main_screen_update_status(
            cloud.wifi_connected,
            cloud.mqtt_connected,
            app_ch32_link_is_ready());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main_services_bind_ui_callbacks(void)
{
    app_ui_set_pickup_callback(app_pickup_cb);
    app_ui_set_exception_demo_callback(app_exception_demo_cb);
    app_ui_set_exception_back_callback(app_exception_back_cb);
    app_ui_set_weather_sim_callback(app_weather_sim_cb);
    app_ui_set_weather_emergency_callback(app_weather_emergency_cb);
}

void app_main_services_start(void)
{
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
