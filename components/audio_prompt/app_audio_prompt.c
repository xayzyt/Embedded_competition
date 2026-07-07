#include "app_audio_prompt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "app_audio_prompt_hw.h"

static const char *TAG = "audio_prompt";

#define APP_AUDIO_PROMPT_TASK_STACK  (4 * 1024)
#define APP_AUDIO_PROMPT_TASK_PRIO   (tskIDLE_PRIORITY + 1)
#define APP_AUDIO_PROMPT_TASK_CORE   1
#define APP_AUDIO_PROMPT_QUEUE_LEN   8
#define APP_AUDIO_PROMPT_CHUNK_BYTES 2048U
#define APP_AUDIO_PROMPT_NVS_NS      "audio_prompt"
#define APP_AUDIO_PROMPT_NVS_KEY     "enabled"

extern const uint8_t s_ready_pcm_start[] asm("_binary_ready_pcm_start");
extern const uint8_t s_ready_pcm_end[] asm("_binary_ready_pcm_end");
extern const uint8_t s_docking_complete_pcm_start[] asm("_binary_docking_complete_pcm_start");
extern const uint8_t s_docking_complete_pcm_end[] asm("_binary_docking_complete_pcm_end");
extern const uint8_t s_outer_door_opening_pcm_start[] asm("_binary_outer_door_opening_pcm_start");
extern const uint8_t s_outer_door_opening_pcm_end[] asm("_binary_outer_door_opening_pcm_end");
extern const uint8_t s_weather_paused_pcm_start[] asm("_binary_weather_paused_pcm_start");
extern const uint8_t s_weather_paused_pcm_end[] asm("_binary_weather_paused_pcm_end");
extern const uint8_t s_tray_retracting_pcm_start[] asm("_binary_tray_retracting_pcm_start");
extern const uint8_t s_tray_retracting_pcm_end[] asm("_binary_tray_retracting_pcm_end");
extern const uint8_t s_apriltag_located_pcm_start[] asm("_binary_apriltag_located_pcm_start");
extern const uint8_t s_apriltag_located_pcm_end[] asm("_binary_apriltag_located_pcm_end");
extern const uint8_t s_drone_identified_pcm_start[] asm("_binary_drone_identified_pcm_start");
extern const uint8_t s_drone_identified_pcm_end[] asm("_binary_drone_identified_pcm_end");

typedef struct {
    app_audio_prompt_id_t id;
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
    bool play_once;
} app_audio_prompt_asset_t;

static const app_audio_prompt_asset_t s_prompt_assets[APP_AUDIO_PROMPT_COUNT] = {
    [APP_AUDIO_PROMPT_READY] = {
        .id = APP_AUDIO_PROMPT_READY,
        .name = "ready",
        .start = s_ready_pcm_start,
        .end = s_ready_pcm_end,
        .play_once = true,
    },
    [APP_AUDIO_PROMPT_DOCKING_COMPLETE] = {
        .id = APP_AUDIO_PROMPT_DOCKING_COMPLETE,
        .name = "docking_complete",
        .start = s_docking_complete_pcm_start,
        .end = s_docking_complete_pcm_end,
    },
    [APP_AUDIO_PROMPT_OUTER_DOOR_OPENING] = {
        .id = APP_AUDIO_PROMPT_OUTER_DOOR_OPENING,
        .name = "outer_door_opening",
        .start = s_outer_door_opening_pcm_start,
        .end = s_outer_door_opening_pcm_end,
    },
    [APP_AUDIO_PROMPT_WEATHER_PAUSED] = {
        .id = APP_AUDIO_PROMPT_WEATHER_PAUSED,
        .name = "weather_paused",
        .start = s_weather_paused_pcm_start,
        .end = s_weather_paused_pcm_end,
    },
    [APP_AUDIO_PROMPT_TRAY_RETRACTING] = {
        .id = APP_AUDIO_PROMPT_TRAY_RETRACTING,
        .name = "tray_retracting",
        .start = s_tray_retracting_pcm_start,
        .end = s_tray_retracting_pcm_end,
    },
    [APP_AUDIO_PROMPT_APRILTAG_LOCATED] = {
        .id = APP_AUDIO_PROMPT_APRILTAG_LOCATED,
        .name = "apriltag_located",
        .start = s_apriltag_located_pcm_start,
        .end = s_apriltag_located_pcm_end,
    },
    [APP_AUDIO_PROMPT_DRONE_IDENTIFIED] = {
        .id = APP_AUDIO_PROMPT_DRONE_IDENTIFIED,
        .name = "drone_identified",
        .start = s_drone_identified_pcm_start,
        .end = s_drone_identified_pcm_end,
    },
};

static QueueHandle_t s_audio_queue = NULL;
static TaskHandle_t s_audio_task = NULL;
static portMUX_TYPE s_audio_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t *s_audio_chunk = NULL;
static bool s_audio_task_uses_caps = false;
static uint32_t s_pending_mask = 0;
static uint32_t s_played_once_mask = 0;
static bool s_audio_user_enabled = true;
static bool s_audio_preference_loaded = false;
static bool s_audio_hw_unavailable = false;
static bool s_audio_hw_warned = false;
static app_audio_prompt_id_t s_playing_prompt = APP_AUDIO_PROMPT_COUNT;

static uint32_t app_audio_prompt_bit(app_audio_prompt_id_t prompt)
{
    return 1UL << (uint32_t)prompt;
}

static const app_audio_prompt_asset_t *app_audio_prompt_asset(app_audio_prompt_id_t prompt)
{
    if ((int)prompt < 0 || prompt >= APP_AUDIO_PROMPT_COUNT)
    {
        return NULL;
    }
    return &s_prompt_assets[prompt];
}

static void app_audio_prompt_load_enabled_once(void)
{
    taskENTER_CRITICAL(&s_audio_mux);
    const bool loaded = s_audio_preference_loaded;
    taskEXIT_CRITICAL(&s_audio_mux);
    if (loaded)
    {
        return;
    }

    bool enabled = true;
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(APP_AUDIO_PROMPT_NVS_NS, NVS_READONLY, &handle);
    if (ret == ESP_OK)
    {
        uint8_t stored = 1U;
        ret = nvs_get_u8(handle, APP_AUDIO_PROMPT_NVS_KEY, &stored);
        if (ret == ESP_OK)
        {
            enabled = stored != 0U;
        }
        nvs_close(handle);
    }

    taskENTER_CRITICAL(&s_audio_mux);
    if (!s_audio_preference_loaded)
    {
        s_audio_user_enabled = enabled;
        s_audio_preference_loaded = true;
    }
    taskEXIT_CRITICAL(&s_audio_mux);
}

static esp_err_t app_audio_prompt_persist_enabled(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(APP_AUDIO_PROMPT_NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = nvs_set_u8(handle, APP_AUDIO_PROMPT_NVS_KEY, enabled ? 1U : 0U);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool app_audio_prompt_hw_available(void)
{
    bool unavailable = false;
    taskENTER_CRITICAL(&s_audio_mux);
    unavailable = s_audio_hw_unavailable;
    taskEXIT_CRITICAL(&s_audio_mux);
    return !unavailable && app_audio_prompt_hw_is_ready();
}

static void app_audio_prompt_mark_hw_unavailable(const char *reason, esp_err_t err)
{
    bool should_log = false;
    taskENTER_CRITICAL(&s_audio_mux);
    s_audio_hw_unavailable = true;
    s_pending_mask = 0;
    s_playing_prompt = APP_AUDIO_PROMPT_COUNT;
    if (!s_audio_hw_warned)
    {
        s_audio_hw_warned = true;
        should_log = true;
    }
    taskEXIT_CRITICAL(&s_audio_mux);

    if (s_audio_queue != NULL)
    {
        (void)xQueueReset(s_audio_queue);
    }
    if (should_log)
    {
        ESP_LOGW(TAG,
            "audio hardware disabled: %s (%s), voice prompts will be skipped",
            reason,
            esp_err_to_name(err));
    }
}

static esp_err_t app_audio_prompt_write_pcm(const uint8_t *pcm, size_t len)
{
    if (pcm == NULL || len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_audio_prompt_hw_available() || s_audio_chunk == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    size_t offset = 0;
    esp_err_t result = ESP_OK;
    while (offset < len)
    {
        if (!app_audio_prompt_is_enabled())
        {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        size_t copy_len = len - offset;
        if (copy_len > APP_AUDIO_PROMPT_CHUNK_BYTES)
        {
            copy_len = APP_AUDIO_PROMPT_CHUNK_BYTES;
        }
        memcpy(s_audio_chunk, pcm + offset, copy_len);
        esp_err_t ret = app_audio_prompt_hw_write(s_audio_chunk, copy_len);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "speaker write failed: %s", esp_err_to_name(ret));
            result = ret;
            break;
        }
        offset += copy_len;
    }

    return result;
}

static void app_audio_prompt_mark_playing(app_audio_prompt_id_t prompt)
{
    taskENTER_CRITICAL(&s_audio_mux);
    s_pending_mask &= ~app_audio_prompt_bit(prompt);
    s_playing_prompt = prompt;
    taskEXIT_CRITICAL(&s_audio_mux);
}

static void app_audio_prompt_mark_finished(const app_audio_prompt_asset_t *asset,
    bool played)
{
    taskENTER_CRITICAL(&s_audio_mux);
    s_playing_prompt = APP_AUDIO_PROMPT_COUNT;
    if (played && asset->play_once)
    {
        s_played_once_mask |= app_audio_prompt_bit(asset->id);
    }
    taskEXIT_CRITICAL(&s_audio_mux);
}

static void app_audio_prompt_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_audio_prompt_id_t prompt = APP_AUDIO_PROMPT_COUNT;
        if (xQueueReceive(s_audio_queue, &prompt, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        const app_audio_prompt_asset_t *asset = app_audio_prompt_asset(prompt);
        if (asset == NULL)
        {
            continue;
        }
        app_audio_prompt_mark_playing(prompt);

        const size_t len = (size_t)(asset->end - asset->start);
        esp_err_t ret = app_audio_prompt_write_pcm(asset->start, len);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "%s prompt played, bytes=%u", asset->name, (unsigned)len);
        }
        else
        {
            ESP_LOGW(TAG, "%s prompt failed: %s", asset->name, esp_err_to_name(ret));
            if (ret != ESP_ERR_INVALID_STATE)
            {
                app_audio_prompt_mark_hw_unavailable("speaker write failed", ret);
                app_audio_prompt_hw_deinit();
            }
        }

        app_audio_prompt_mark_finished(asset, ret == ESP_OK);
    }
}

static void app_audio_prompt_cleanup_software(void)
{
    if (s_audio_task != NULL)
    {
        if (s_audio_task_uses_caps)
        {
            vTaskDeleteWithCaps(s_audio_task);
        }
        else
        {
            vTaskDelete(s_audio_task);
        }
        s_audio_task = NULL;
        s_audio_task_uses_caps = false;
    }
    if (s_audio_queue != NULL)
    {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }
    if (s_audio_chunk != NULL)
    {
        heap_caps_free(s_audio_chunk);
        s_audio_chunk = NULL;
    }
}

static esp_err_t app_audio_prompt_create_task(void)
{
#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(app_audio_prompt_task,
        "audio_prompt",
        APP_AUDIO_PROMPT_TASK_STACK,
        NULL,
        APP_AUDIO_PROMPT_TASK_PRIO,
        &s_audio_task,
        APP_AUDIO_PROMPT_TASK_CORE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok == pdPASS)
    {
        s_audio_task_uses_caps = true;
        return ESP_OK;
    }
    ESP_LOGW(TAG, "create audio task with PSRAM stack failed, try internal stack");
#endif

    BaseType_t fallback_ok = xTaskCreatePinnedToCore(app_audio_prompt_task,
        "audio_prompt",
        APP_AUDIO_PROMPT_TASK_STACK,
        NULL,
        APP_AUDIO_PROMPT_TASK_PRIO,
        &s_audio_task,
        APP_AUDIO_PROMPT_TASK_CORE);
    if (fallback_ok != pdPASS)
    {
        s_audio_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_audio_task_uses_caps = false;
    return ESP_OK;
}

esp_err_t app_audio_prompt_init(void)
{
    app_audio_prompt_load_enabled_once();
    if (app_audio_prompt_hw_available() && s_audio_task != NULL)
    {
        return ESP_OK;
    }
    taskENTER_CRITICAL(&s_audio_mux);
    const bool hw_unavailable = s_audio_hw_unavailable;
    taskEXIT_CRITICAL(&s_audio_mux);
    if (hw_unavailable)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (s_audio_queue == NULL)
    {
        s_audio_queue = xQueueCreate(APP_AUDIO_PROMPT_QUEUE_LEN,
            sizeof(app_audio_prompt_id_t));
        if (s_audio_queue == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    s_audio_chunk = (uint8_t *)heap_caps_malloc(APP_AUDIO_PROMPT_CHUNK_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_audio_chunk == NULL)
    {
        ESP_LOGW(TAG, "allocate persistent audio chunk in PSRAM failed");
        app_audio_prompt_cleanup_software();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = app_audio_prompt_create_task();
    if (ret != ESP_OK)
    {
        app_audio_prompt_cleanup_software();
        return ret;
    }

    ret = app_audio_prompt_hw_init();
    if (ret != ESP_OK)
    {
        app_audio_prompt_hw_deinit();
        app_audio_prompt_cleanup_software();
        app_audio_prompt_mark_hw_unavailable("startup hardware initialization failed", ret);
        return ret;
    }

    ESP_LOGI(TAG,
        "voice prompt ready, user_enabled=%d task_core=%u stack=%s",
        app_audio_prompt_is_enabled(),
        (unsigned)APP_AUDIO_PROMPT_TASK_CORE,
        s_audio_task_uses_caps ? "psram" : "internal");
    return ESP_OK;
}

bool app_audio_prompt_is_enabled(void)
{
    app_audio_prompt_load_enabled_once();
    taskENTER_CRITICAL(&s_audio_mux);
    const bool enabled = s_audio_user_enabled && !s_audio_hw_unavailable;
    taskEXIT_CRITICAL(&s_audio_mux);
    return enabled && app_audio_prompt_hw_is_ready();
}

esp_err_t app_audio_prompt_set_enabled(bool enabled, bool persist)
{
    app_audio_prompt_load_enabled_once();

    taskENTER_CRITICAL(&s_audio_mux);
    s_audio_user_enabled = enabled;
    s_audio_preference_loaded = true;
    if (!enabled)
    {
        s_pending_mask = 0;
    }
    taskEXIT_CRITICAL(&s_audio_mux);

    if (!enabled && s_audio_queue != NULL)
    {
        (void)xQueueReset(s_audio_queue);
    }

    esp_err_t persist_ret = ESP_OK;
    if (persist)
    {
        persist_ret = app_audio_prompt_persist_enabled(enabled);
        if (persist_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "persist audio enabled failed: %s", esp_err_to_name(persist_ret));
        }
    }

    if (enabled && !app_audio_prompt_hw_available())
    {
        ESP_LOGW(TAG,
            "voice preference enabled, but audio hardware is unavailable this session");
        return ESP_ERR_INVALID_STATE;
    }
    return persist_ret;
}

esp_err_t app_audio_prompt_request(app_audio_prompt_id_t prompt)
{
    const app_audio_prompt_asset_t *asset = app_audio_prompt_asset(prompt);
    if (asset == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_audio_prompt_is_enabled())
    {
        return ESP_OK;
    }
    if (!app_audio_prompt_hw_available())
    {
        return ESP_OK;
    }
    if (s_audio_queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t bit = app_audio_prompt_bit(prompt);
    taskENTER_CRITICAL(&s_audio_mux);
    const bool already_pending = (s_pending_mask & bit) != 0U;
    const bool already_playing = s_playing_prompt == prompt;
    const bool already_played_once = asset->play_once &&
        ((s_played_once_mask & bit) != 0U);
    if (already_pending || already_playing || already_played_once)
    {
        taskEXIT_CRITICAL(&s_audio_mux);
        return ESP_OK;
    }
    s_pending_mask |= bit;
    taskEXIT_CRITICAL(&s_audio_mux);

    if (xQueueSend(s_audio_queue, &prompt, 0) != pdPASS)
    {
        taskENTER_CRITICAL(&s_audio_mux);
        s_pending_mask &= ~bit;
        taskEXIT_CRITICAL(&s_audio_mux);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t app_audio_prompt_request_ready(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_READY);
}

esp_err_t app_audio_prompt_request_docking_complete(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_DOCKING_COMPLETE);
}

esp_err_t app_audio_prompt_request_outer_door_opening(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_OUTER_DOOR_OPENING);
}

esp_err_t app_audio_prompt_request_weather_paused(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_WEATHER_PAUSED);
}

esp_err_t app_audio_prompt_request_tray_retracting(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_TRAY_RETRACTING);
}

esp_err_t app_audio_prompt_request_apriltag_located(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_APRILTAG_LOCATED);
}

esp_err_t app_audio_prompt_request_drone_identified(void)
{
    return app_audio_prompt_request(APP_AUDIO_PROMPT_DRONE_IDENTIFIED);
}
