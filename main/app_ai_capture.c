#include "app_ai_capture.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "app_ui.h"

#define CAPTURE_DIR                 BSP_SD_MOUNT_POINT "/CAP"
#define CAPTURE_WIDTH               320U
#define CAPTURE_HEIGHT              240U
#define CAPTURE_CHANNELS            3U
#define CAPTURE_FRAME_BYTES         (CAPTURE_WIDTH * CAPTURE_HEIGHT * CAPTURE_CHANNELS)
#define CAPTURE_FRAME_INTERVAL      10U
#define CAPTURE_SLOT_COUNT          2
#define CAPTURE_TASK_STACK_SIZE     (6 * 1024)
#define CAPTURE_TASK_PRIORITY       3
#define CAPTURE_TASK_CORE_ID        1
#define CAPTURE_MAX_INDEX           99999U

typedef struct {
    uint8_t *bgr;
    uint32_t image_index;
} app_ai_capture_slot_t;

static const char *TAG = "app_ai_capture";

static bool s_inited = false;
static bool s_sd_ready = false;
static bool s_active = false;
static uint32_t s_frame_skip = 0;
static uint32_t s_saved_count = 0;
static uint32_t s_drop_count = 0;
static uint32_t s_wait_count = 0;
static uint32_t s_next_index = 1;
static TaskHandle_t s_writer_task = NULL;
static QueueHandle_t s_free_queue = NULL;
static QueueHandle_t s_write_queue = NULL;
static app_ai_capture_slot_t s_slots[CAPTURE_SLOT_COUNT] = {0};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline uint8_t app_ai_capture_rgb565_r(uint16_t pixel)
{
    uint8_t r = (uint8_t)((pixel >> 11) & 0x1F);
    return (uint8_t)((r << 3) | (r >> 2));
}

static inline uint8_t app_ai_capture_rgb565_g(uint16_t pixel)
{
    uint8_t g = (uint8_t)((pixel >> 5) & 0x3F);
    return (uint8_t)((g << 2) | (g >> 4));
}

static inline uint8_t app_ai_capture_rgb565_b(uint16_t pixel)
{
    uint8_t b = (uint8_t)(pixel & 0x1F);
    return (uint8_t)((b << 3) | (b >> 2));
}

static void app_ai_capture_put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void app_ai_capture_put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void app_ai_capture_put_le_i32(uint8_t *dst, int32_t value)
{
    app_ai_capture_put_le32(dst, (uint32_t)value);
}

static bool app_ai_capture_parse_image_name(const char *name, uint32_t *out_index)
{
    if (name == NULL || out_index == NULL || strlen(name) != 12U) {
        return false;
    }
    if (name[0] != 'I' || name[1] != 'M' || name[2] != 'G' ||
        name[8] != '.' || name[9] != 'B' || name[10] != 'M' || name[11] != 'P') {
        return false;
    }

    uint32_t value = 0;
    for (int i = 3; i < 8; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = (value * 10U) + (uint32_t)(name[i] - '0');
    }
    if (value == 0 || value > CAPTURE_MAX_INDEX) {
        return false;
    }
    *out_index = value;
    return true;
}

static uint32_t app_ai_capture_find_next_index(void)
{
    uint32_t max_index = 0;
    DIR *dir = opendir(CAPTURE_DIR);
    if (dir == NULL) {
        return 1;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        uint32_t index = 0;
        if (app_ai_capture_parse_image_name(ent->d_name, &index) && index > max_index) {
            max_index = index;
        }
    }
    closedir(dir);

    if (max_index >= CAPTURE_MAX_INDEX) {
        return CAPTURE_MAX_INDEX;
    }
    return max_index + 1U;
}

static void app_ai_capture_set_status(const char *status)
{
    if (status != NULL) {
        app_ui_set_capture_text(status);
        app_ui_set_vision_text(status);
    }
}

static void app_ai_capture_format_status(const char *state)
{
    char text[64];
    uint32_t saved = 0;
    uint32_t dropped = 0;

    taskENTER_CRITICAL(&s_mux);
    saved = s_saved_count;
    dropped = s_drop_count;
    taskEXIT_CRITICAL(&s_mux);

    if (dropped == 0) {
        snprintf(text,
                 sizeof(text),
                 "cap: %s #%lu",
                 state != NULL ? state : "-",
                 (unsigned long)saved);
    } else {
        snprintf(text,
                 sizeof(text),
                 "cap: %s #%lu drop:%lu",
                 state != NULL ? state : "-",
                 (unsigned long)saved,
                 (unsigned long)dropped);
    }
    app_ai_capture_set_status(text);
}

static esp_err_t app_ai_capture_prepare_storage(void)
{
    if (s_sd_ready) {
        return ESP_OK;
    }

    esp_err_t ret = bsp_sdcard_mount();
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sd mount failed: %s", esp_err_to_name(ret));
        app_ai_capture_set_status("cap: sd fail");
        return ret;
    }

    if (mkdir(CAPTURE_DIR, 0775) != 0 && errno != EEXIST) {
        ret = ESP_FAIL;
        ESP_LOGW(TAG, "mkdir %s failed, errno=%d", CAPTURE_DIR, errno);
        app_ai_capture_set_status("cap: mkdir fail");
        return ret;
    }

    s_next_index = app_ai_capture_find_next_index();
    s_sd_ready = true;
    ESP_LOGI(TAG, "capture dir ready: %s next=%lu", CAPTURE_DIR, (unsigned long)s_next_index);
    return ESP_OK;
}

static void app_ai_capture_downsample_rgb565_to_bgr(const uint8_t *rgb565,
                                                    uint32_t src_width,
                                                    uint32_t src_height,
                                                    uint8_t *dst_bgr)
{
    const uint16_t *src = (const uint16_t *)rgb565;
    for (uint32_t y = 0; y < CAPTURE_HEIGHT; y++) {
        uint32_t sy = (y * src_height) / CAPTURE_HEIGHT;
        if (sy >= src_height) {
            sy = src_height - 1U;
        }
        const uint16_t *src_row = src + ((size_t)sy * src_width);
        uint8_t *dst_row = dst_bgr + ((size_t)y * CAPTURE_WIDTH * CAPTURE_CHANNELS);
        for (uint32_t x = 0; x < CAPTURE_WIDTH; x++) {
            uint32_t sx = (x * src_width) / CAPTURE_WIDTH;
            if (sx >= src_width) {
                sx = src_width - 1U;
            }
            uint16_t pixel = src_row[sx];
            uint8_t *p = dst_row + ((size_t)x * CAPTURE_CHANNELS);
            p[0] = app_ai_capture_rgb565_b(pixel);
            p[1] = app_ai_capture_rgb565_g(pixel);
            p[2] = app_ai_capture_rgb565_r(pixel);
        }
    }
}

static esp_err_t app_ai_capture_write_bmp(const app_ai_capture_slot_t *slot)
{
    if (slot == NULL || slot->bgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot->image_index > CAPTURE_MAX_INDEX) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[64];
    snprintf(path, sizeof(path), CAPTURE_DIR "/IMG%05lu.BMP", (unsigned long)slot->image_index);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "open %s failed, errno=%d", path, errno);
        return ESP_FAIL;
    }

    const uint32_t row_bytes = CAPTURE_WIDTH * CAPTURE_CHANNELS;
    const uint32_t image_bytes = row_bytes * CAPTURE_HEIGHT;
    const uint32_t file_bytes = 54U + image_bytes;
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    app_ai_capture_put_le32(&header[2], file_bytes);
    app_ai_capture_put_le32(&header[10], 54U);
    app_ai_capture_put_le32(&header[14], 40U);
    app_ai_capture_put_le32(&header[18], CAPTURE_WIDTH);
    app_ai_capture_put_le_i32(&header[22], -(int32_t)CAPTURE_HEIGHT);
    app_ai_capture_put_le16(&header[26], 1U);
    app_ai_capture_put_le16(&header[28], 24U);
    app_ai_capture_put_le32(&header[34], image_bytes);

    esp_err_t ret = ESP_OK;
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK && fwrite(slot->bgr, 1, image_bytes, file) != image_bytes) {
        ret = ESP_FAIL;
    }

    if (fclose(file) != 0 && ret == ESP_OK) {
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "saved %s", path);
    } else {
        ESP_LOGW(TAG, "write %s failed", path);
    }
    return ret;
}

static void app_ai_capture_writer_task(void *arg)
{
    (void)arg;
    while (1) {
        app_ai_capture_slot_t *slot = NULL;
        if (xQueueReceive(s_write_queue, &slot, portMAX_DELAY) != pdTRUE || slot == NULL) {
            continue;
        }

        esp_err_t ret = app_ai_capture_write_bmp(slot);

        taskENTER_CRITICAL(&s_mux);
        if (ret == ESP_OK) {
            s_saved_count++;
        } else {
            s_drop_count++;
        }
        taskEXIT_CRITICAL(&s_mux);

        app_ai_capture_format_status(ret == ESP_OK ? "saved" : "write fail");
        (void)xQueueSend(s_free_queue, &slot, portMAX_DELAY);
    }
}

esp_err_t app_ai_capture_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_free_queue = xQueueCreate(CAPTURE_SLOT_COUNT, sizeof(app_ai_capture_slot_t *));
    s_write_queue = xQueueCreate(CAPTURE_SLOT_COUNT, sizeof(app_ai_capture_slot_t *));
    if (s_free_queue == NULL || s_write_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < CAPTURE_SLOT_COUNT; i++) {
        s_slots[i].bgr = heap_caps_malloc(CAPTURE_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_slots[i].bgr == NULL) {
            return ESP_ERR_NO_MEM;
        }
        app_ai_capture_slot_t *slot = &s_slots[i];
        if (xQueueSend(s_free_queue, &slot, 0) != pdTRUE) {
            return ESP_FAIL;
        }
    }

    BaseType_t ok = xTaskCreatePinnedToCore(app_ai_capture_writer_task,
                                            "capture_writer",
                                            CAPTURE_TASK_STACK_SIZE,
                                            NULL,
                                            CAPTURE_TASK_PRIORITY,
                                            &s_writer_task,
                                            CAPTURE_TASK_CORE_ID);
    if (ok != pdPASS) {
        s_writer_task = NULL;
        return ESP_FAIL;
    }

    s_inited = true;
    (void)app_ai_capture_prepare_storage();
    app_ai_capture_format_status("off");
    return ESP_OK;
}

esp_err_t app_ai_capture_start(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = app_ai_capture_prepare_storage();
    if (ret != ESP_OK) {
        return ret;
    }

    taskENTER_CRITICAL(&s_mux);
    s_active = true;
    s_frame_skip = 0;
    s_wait_count = 0;
    taskEXIT_CRITICAL(&s_mux);

    app_ai_capture_format_status("on");
    ESP_LOGI(TAG, "continuous capture started");
    return ESP_OK;
}

void app_ai_capture_stop(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_active = false;
    s_frame_skip = 0;
    taskEXIT_CRITICAL(&s_mux);

    app_ai_capture_format_status("stopped");
    ESP_LOGI(TAG, "continuous capture stopped");
}

bool app_ai_capture_is_active(void)
{
    bool active = false;
    taskENTER_CRITICAL(&s_mux);
    active = s_active;
    taskEXIT_CRITICAL(&s_mux);
    return active;
}

bool app_ai_capture_should_capture_frame(void)
{
    bool capture = false;
    bool active = false;
    bool sd_ready = false;

    taskENTER_CRITICAL(&s_mux);
    active = s_active;
    sd_ready = s_sd_ready;
    taskEXIT_CRITICAL(&s_mux);

    if (active && sd_ready) {
        if (s_free_queue != NULL && uxQueueMessagesWaiting(s_free_queue) == 0) {
            taskENTER_CRITICAL(&s_mux);
            s_wait_count++;
            s_frame_skip = 0;
            taskEXIT_CRITICAL(&s_mux);
        } else {
            taskENTER_CRITICAL(&s_mux);
            s_frame_skip++;
            if (s_frame_skip >= CAPTURE_FRAME_INTERVAL) {
                s_frame_skip = 0;
                capture = true;
            }
            taskEXIT_CRITICAL(&s_mux);
        }
    }

    return capture;
}

esp_err_t app_ai_capture_submit_frame(const uint8_t *rgb565,
                                      uint32_t width,
                                      uint32_t height,
                                      size_t len)
{
    if (rgb565 == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t min_len = (size_t)width * (size_t)height * 2U;
    if (len < min_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    app_ai_capture_slot_t *slot = NULL;
    if (xQueueReceive(s_free_queue, &slot, 0) != pdTRUE || slot == NULL) {
        taskENTER_CRITICAL(&s_mux);
        s_wait_count++;
        taskEXIT_CRITICAL(&s_mux);
        app_ai_capture_format_status("wait");
        return ESP_ERR_NO_MEM;
    }

    taskENTER_CRITICAL(&s_mux);
    slot->image_index = s_next_index;
    if (s_next_index < CAPTURE_MAX_INDEX) {
        s_next_index++;
    }
    taskEXIT_CRITICAL(&s_mux);

    app_ai_capture_downsample_rgb565_to_bgr(rgb565, width, height, slot->bgr);

    if (xQueueSend(s_write_queue, &slot, 0) != pdTRUE) {
        taskENTER_CRITICAL(&s_mux);
        s_drop_count++;
        taskEXIT_CRITICAL(&s_mux);
        (void)xQueueSend(s_free_queue, &slot, 0);
        app_ai_capture_format_status("busy");
        return ESP_ERR_TIMEOUT;
    }

    app_ai_capture_format_status("queued");
    return ESP_OK;
}
