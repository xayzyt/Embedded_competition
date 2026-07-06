#include "app_cloud.h"
#include "app_cloud_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "sdkconfig.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "app_ch32_link.h"
#include "app_audio_prompt.h"
#include "app_ui.h"

// 天气子模块：拉取心知天气、缓存主屏展示，并在恶劣天气时阻止/中止对接。

static const char *TAG = "app_cloud";
static TaskHandle_t s_weather_task = NULL;
typedef struct {
    char *buf;       // 调用方提供的 HTTP 响应缓存。
    int len;         // 已写入字节数，不含末尾 '\0'。
    int cap;         // 可写数据容量。
    bool truncated; // 响应是否超过固定缓存。
} app_cloud_http_buf_t;

/* ---------- HTTP 响应与天气解析 ---------- */

// 判断当前任务是否已经进入需要天气保护介入的活动阶段。
static bool app_cloud_snapshot_is_active_task(const app_task_snapshot_t *snap)
{
    if (snap == NULL)
    {
        return false;
    }
    return snap->active ||
        snap->state == APP_TASK_STATE_WAIT_APPROACH ||
        snap->state == APP_TASK_STATE_AUTH_PASSED ||
        snap->state == APP_TASK_STATE_DOCKING;
}
// HTTP 回调按块累积响应体，超过固定缓冲区则标记截断。
static esp_err_t app_cloud_weather_http_event_cb(esp_http_client_event_t *evt)
{
    if (evt == NULL || evt->event_id != HTTP_EVENT_ON_DATA)
    {
        return ESP_OK;
    }
    app_cloud_http_buf_t *rx = (app_cloud_http_buf_t *)evt->user_data;
    if (rx == NULL || rx->buf == NULL || rx->cap <= 0 || evt->data == NULL || evt->data_len <= 0)
    {
        return ESP_OK;
    }
    int free_len = rx->cap - rx->len;
    if (free_len <= 0)
    {
        rx->truncated = true;
        return ESP_OK;
    }
    int copy_len = evt->data_len;
    if (copy_len > free_len)
    {
        copy_len = free_len;
        rx->truncated = true;
    }
    memcpy(rx->buf + rx->len, evt->data, (size_t)copy_len);
    rx->len += copy_len;
    rx->buf[rx->len] = '\0';
    return ESP_OK;
}
static const char *app_cloud_json_get_string(cJSON *obj, const char *key)
{
    if (obj == NULL || key == NULL)
    {
        return NULL;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        return item->valuestring;
    }
    return NULL;
}
// 解析心知天气 now.json，提取天气文字、温度和天气 code。
static esp_err_t app_cloud_parse_weather_response(const char *json,
    char *weather_text,
    size_t weather_text_size,
    int *weather_code)
{
    if (json == NULL || weather_text == NULL || weather_text_size == 0 || weather_code == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t ret = ESP_ERR_INVALID_RESPONSE;
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    cJSON *first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : NULL;
    cJSON *now = first ? cJSON_GetObjectItemCaseSensitive(first, "now") : NULL;
    const char *text = app_cloud_json_get_string(now, "text");
    const char *code = app_cloud_json_get_string(now, "code");
    const char *temp = app_cloud_json_get_string(now, "temperature");
    if (text != NULL && code != NULL && temp != NULL)
    {
        *weather_code = atoi(code);
        snprintf(weather_text,
            weather_text_size,
            "%s\n%s℃",
            text,
            temp);
        ret = ESP_OK;
    }
    cJSON_Delete(root);
    return ret;
}
// 缓存最近一次真实天气，退出模拟模式时可快速恢复显示。
/* ---------- 天气缓存与保护策略 ---------- */

static void app_cloud_cache_weather(const char *text, int weather_code)
{
    if (text == NULL)
    {
        return;
    }
    strlcpy(s_cloud.cached_weather_text, text, sizeof(s_cloud.cached_weather_text));
    s_cloud.cached_weather_code = weather_code;
    s_cloud.have_cached_weather = true;
    s_cloud.weather_cache_tick = xTaskGetTickCount();
}
static bool app_cloud_cached_weather_is_fresh(TickType_t max_age_ticks)
{
    if (!s_cloud.have_cached_weather)
    {
        return false;
    }
    return (xTaskGetTickCount() - s_cloud.weather_cache_tick) < max_age_ticks;
}
// 唤醒天气任务，用于 Wi-Fi 连上、模拟状态切换或紧急保护后立即刷新。
void app_cloud_notify_weather_task(void)
{
    if (s_weather_task != NULL)
    {
        xTaskNotifyGive(s_weather_task);
    }
}
// 未配置天气 API key 或请求失败时使用本地兜底天气。
static void app_cloud_show_weather_fallback(void)
{
    const char *fallback = "多云\n--℃";
    app_cloud_cache_weather(fallback, APP_CLOUD_WEATHER_FALLBACK_CODE);
    app_ui_main_screen_apply_weather_state(fallback,
        APP_CLOUD_WEATHER_FALLBACK_CODE,
        false,
        NULL);
}
// 恶劣天气紧急保护：优先安全关闭，失败再请求中止。
static esp_err_t app_cloud_send_weather_protection(void)
{
    if (!app_ch32_link_is_ready())
    {
        ESP_LOGW(TAG, "CH32 not ready, try SAFE_CLOSE anyway");
    }
    esp_err_t ret = app_ch32_link_send_proto_cmd_and_wait_ack(APP_CH32_PROTO_CMD_SAFE_CLOSE, 3000);
    if (ret == ESP_OK)
    {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "SAFE_CLOSE failed, fallback abort: %s", esp_err_to_name(ret));
    ret = app_ch32_link_send_proto_cmd_and_wait_ack(APP_CH32_PROTO_CMD_ABORT, APP_CLOUD_ABORT_WAIT_MS);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "abort dock for severe weather failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
// 将天气策略应用到任务状态机和 CH32 执行机构。
static esp_err_t app_cloud_apply_weather_docking_policy(bool simulated)
{
    s_cloud.weather_docking_blocked = simulated;
    if (!simulated)
    {
        s_cloud.weather_docking_policy_applied = false;
        return ESP_OK;
    }
    if (s_cloud.weather_docking_policy_applied)
    {
        return ESP_OK;
    }
    s_cloud.weather_docking_policy_applied = true;
    (void)app_audio_prompt_request_weather_paused();
    esp_err_t ret = ESP_OK;
    app_task_snapshot_t snap = {0};
    const bool active_task = app_task_peek_snapshot(&snap) && app_cloud_snapshot_is_active_task(&snap);
    const bool safety_task = active_task && strcmp(snap.source, "safety") == 0;
    bool task_policy_applied = false;
    if (active_task && !safety_task)
    {
        if (s_cloud.weather_mode == APP_CLOUD_WEATHER_MODE_EMERGENCY)
        {
            task_policy_applied =
                app_task_mark_fault_if_current(&snap, "恶劣天气，已终止接驳保护");
        }
        else
        {
            task_policy_applied =
                app_task_cancel_if_current(&snap, "blocked by simulated severe weather");
        }
    }
    if (s_cloud.weather_mode == APP_CLOUD_WEATHER_MODE_EMERGENCY)
    {
        ret = app_cloud_send_weather_protection();
    }
    else if (task_policy_applied && app_ch32_link_is_ready())
    {
        ret = app_ch32_link_send_proto_cmd_and_wait_ack(APP_CH32_PROTO_CMD_ABORT, APP_CLOUD_ABORT_WAIT_MS);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "abort dock for severe weather failed: %s", esp_err_to_name(ret));
        }
    }
    return ret;
}
// 主屏展示模拟恶劣天气，并把任务状态置为天气阻止。
static void app_cloud_show_simulated_weather_ui(void)
{
    app_ui_main_screen_apply_weather_state("台风\n28℃",
        APP_CLOUD_WEATHER_SEVERE_CODE,
        true,
        NULL);
    app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
}
static void app_cloud_show_simulated_severe_weather(void)
{
    app_cloud_show_simulated_weather_ui();
    app_cloud_apply_weather_docking_policy(true);
}
// 退出模拟后先恢复缓存天气；没有缓存时显示同步中。
static void app_cloud_show_restored_weather_ui(bool update_task_text)
{
    if (s_cloud.have_cached_weather)
    {
        app_ui_main_screen_apply_weather_state(s_cloud.cached_weather_text,
            s_cloud.cached_weather_code,
            false,
            NULL);
        if (update_task_text)
        {
            app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WAITING);
        }
        return;
    }
    app_ui_main_screen_apply_weather_state("同步中", 99, false, NULL);
    if (update_task_text)
    {
        app_ui_main_screen_set_task_state(APP_UI_MAIN_TASK_WAITING);
    }
}
static void app_cloud_show_restored_weather(bool update_task_text)
{
    app_cloud_apply_weather_docking_policy(false);
    s_cloud.weather_mode = APP_CLOUD_WEATHER_MODE_NORMAL;
    app_cloud_show_restored_weather_ui(update_task_text);
}
// 拉取一次真实天气；若中途进入模拟模式，则立即切换到模拟保护。
static esp_err_t app_cloud_fetch_weather_once(void)
{
    if (s_cloud.weather_simulated)
    {
        app_cloud_show_simulated_severe_weather();
        return ESP_OK;
    }
    if (CONFIG_SKY_WEATHER_API_KEY[0] == '\0')
    {
        ESP_LOGW(TAG, "CONFIG_SKY_WEATHER_API_KEY is empty, show local weather fallback");
        app_cloud_show_weather_fallback();
        s_cloud.weather_docking_blocked = false;
        s_cloud.weather_mode = APP_CLOUD_WEATHER_MODE_NORMAL;
        return ESP_OK;
    }
    char url[APP_CLOUD_WEATHER_URL_LEN] = {0};
    snprintf(url,
        sizeof(url),
        "https://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c",
        CONFIG_SKY_WEATHER_API_KEY,
        APP_CLOUD_WEATHER_CITY);
    char response[APP_CLOUD_WEATHER_RESP_LEN] = {0};
    app_cloud_http_buf_t rx = {
        .buf = response,
        .len = 0,
        .cap = (int)sizeof(response) - 1,
        .truncated = false,
    };
    esp_http_client_config_t http_cfg = {
        .url = url,
        .event_handler = app_cloud_weather_http_event_cb,
        .user_data = &rx,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        .timeout_ms = 10000,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL)
    {
        app_ui_main_screen_set_weather("同步失败", 99);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (ret != ESP_OK || status != 200 || rx.len <= 0 || rx.truncated)
    {
        ESP_LOGW(TAG,
            "weather fetch failed: ret=%s http=%d len=%d trunc=%d",
            esp_err_to_name(ret),
            status,
            rx.len,
            rx.truncated);
        app_ui_main_screen_set_weather("同步失败", 99);
        return (ret != ESP_OK) ? ret : ESP_FAIL;
    }
    char weather_text[APP_CLOUD_WEATHER_TEXT_LEN] = {0};
    int weather_code = 99;
    ret = app_cloud_parse_weather_response(response,
        weather_text,
        sizeof(weather_text),
        &weather_code);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "weather json parse failed: %s", esp_err_to_name(ret));
        app_ui_main_screen_set_weather("同步失败", 99);
        return ret;
    }
    if (s_cloud.weather_simulated)
    {
        app_cloud_show_simulated_severe_weather();
        return ESP_OK;
    }
    app_cloud_cache_weather(weather_text, weather_code);
    s_cloud.weather_docking_blocked = false;
    s_cloud.weather_mode = APP_CLOUD_WEATHER_MODE_NORMAL;
    app_ui_main_screen_apply_weather_state(weather_text, weather_code, false, NULL);
    return ESP_OK;
}
// 天气任务：定时刷新真实天气，模拟/恢复状态变化时被通知唤醒。
/* ---------- 后台刷新任务与公共控制接口 ---------- */

static void app_cloud_weather_task(void *arg)
{
    (void)arg;
    app_cloud_show_restored_weather_ui(false);
    for (;;)
    {
        bool skip_fetch_once = false;
        if (s_cloud.weather_simulated)
        {
            app_cloud_show_simulated_severe_weather();
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_CLOUD_WEATHER_REFRESH_MS));
            continue;
        }
        const bool restore_pending = s_cloud.weather_restore_pending;
        s_cloud.weather_restore_pending = false;
        if (restore_pending)
        {
            app_cloud_show_restored_weather(true);
            skip_fetch_once = app_cloud_cached_weather_is_fresh(
                pdMS_TO_TICKS(APP_CLOUD_WEATHER_RESTORE_REFETCH_MIN_MS));
        }
        else
        {
            app_ui_main_screen_set_weather_simulated(false);
        }
        if (skip_fetch_once)
        {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_CLOUD_WEATHER_RESTORE_REFETCH_MIN_MS));
            continue;
        }
        if (!app_cloud_is_wifi_connected())
        {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
            continue;
        }
        if (!app_cloud_is_mqtt_connected())
        {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
            continue;
        }
        esp_err_t ret = app_cloud_fetch_weather_once();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((ret == ESP_OK) ?
            APP_CLOUD_WEATHER_REFRESH_MS :
            APP_CLOUD_WEATHER_RETRY_MS));
    }
}
// 确保天气任务只创建一次。
void app_cloud_start_weather_task_once(void)
{
    if (s_weather_task != NULL)
    {
        return;
    }
    BaseType_t ok = xTaskCreate(app_cloud_weather_task,
        "weather",
        APP_CLOUD_WEATHER_TASK_STACK,
        NULL,
        APP_CLOUD_WEATHER_TASK_PRIO,
        &s_weather_task);
    if (ok != pdPASS)
    {
        ESP_LOGW(TAG, "create weather task failed");
        s_weather_task = NULL;
    }
}
// UI/云端切换天气模拟；带防抖，避免按钮连点反复触发保护。
void app_cloud_set_weather_simulated(bool simulated)
{
    const TickType_t now = xTaskGetTickCount();
    if (s_cloud.weather_last_toggle_tick != 0 &&
        (now - s_cloud.weather_last_toggle_tick) < pdMS_TO_TICKS(APP_CLOUD_WEATHER_TOGGLE_DEBOUNCE_MS))
    {
        return;
    }
    const bool was_simulated = s_cloud.weather_simulated;
    if (was_simulated == simulated)
    {
        if (simulated)
        {
            app_cloud_show_simulated_weather_ui();
        }
        else
        {
            app_cloud_show_restored_weather_ui(false);
        }
        app_cloud_publish_current_state();
        app_cloud_notify_weather_task();
        return;
    }
    s_cloud.weather_last_toggle_tick = now;
    s_cloud.weather_simulated = simulated;
    const bool restore_pending = !simulated && (was_simulated || s_cloud.weather_docking_blocked);
    if (simulated)
    {
        s_cloud.weather_docking_blocked = true;
        s_cloud.weather_mode = APP_CLOUD_WEATHER_MODE_CLOUD_GUARD;
        s_cloud.weather_restore_pending = false;
        app_cloud_show_simulated_weather_ui();
        ESP_LOGW(TAG, "simulate severe weather requested");
    }
    else
    {
        s_cloud.weather_docking_blocked = false;
        s_cloud.weather_docking_policy_applied = false;
        s_cloud.weather_mode = APP_CLOUD_WEATHER_MODE_NORMAL;
        s_cloud.weather_restore_pending = restore_pending;
        app_cloud_show_restored_weather_ui(true);
    }
    app_cloud_publish_current_state();
    app_cloud_notify_weather_task();
}
// 紧急保护入口：强制进入恶劣天气模式并立即执行保护策略。
esp_err_t app_cloud_trigger_weather_emergency_wait(void)
{
    s_cloud.weather_simulated = true;
    s_cloud.weather_docking_blocked = true;
    s_cloud.weather_docking_policy_applied = false;
    s_cloud.weather_restore_pending = false;
    s_cloud.weather_mode = APP_CLOUD_WEATHER_MODE_EMERGENCY;
    app_cloud_show_simulated_weather_ui();
    app_cloud_publish_current_state();
    esp_err_t ret = app_cloud_apply_weather_docking_policy(true);
    app_cloud_notify_weather_task();
    return ret;
}

esp_err_t app_cloud_trigger_weather_demo_protection_wait(void)
{
    return app_cloud_send_weather_protection();
}

void app_cloud_trigger_weather_emergency(void)
{
    (void)app_cloud_trigger_weather_emergency_wait();
}
bool app_cloud_is_weather_simulated(void)
{
    return s_cloud.weather_simulated;
}
bool app_cloud_is_weather_docking_blocked(void)
{
    return s_cloud.weather_docking_blocked;
}
