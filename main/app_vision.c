#include "app_vision.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_apriltag.h"
#include "app_ui.h"
#define VISION_TASK_STACK_SIZE         (16 * 1024)
#define VISION_TASK_PRIORITY           4
#define VISION_TASK_CORE_ID            1
#define VISION_POLL_PERIOD_MS          25
#define VISION_HEARTBEAT_MS            1000
#define VISION_GRAY_WIDTH              240
#define VISION_GRAY_HEIGHT             180
#define VISION_GRAY_BUF_SIZE           (VISION_GRAY_WIDTH * VISION_GRAY_HEIGHT)
#define VISION_LOST_RESET_FRAMES       2U
#define VISION_STABLE_DECAY_ON_LOST    1U
static const char *TAG = "app_vision";
typedef struct {
    app_vision_gray_frame_info_t info;
    uint8_t gray[VISION_GRAY_BUF_SIZE];
} app_vision_gray_slot_t;
static TaskHandle_t s_vision_task = NULL;
static bool s_vision_inited = false;
static portMUX_TYPE s_vision_mux = portMUX_INITIALIZER_UNLOCKED;
static app_vision_frame_info_t s_latest_frame = {0};
static app_vision_gray_slot_t s_gray_slot = {0};
static app_vision_gray_slot_t s_task_slot = {0};
static app_vision_gray_slot_t s_submit_slot = {0};
static app_vision_result_t s_latest_result = {0};
static uint32_t s_submit_seq = 0;
static uint32_t s_submit_overwrite = 0;
static bool s_first_submit_logged = false;
static inline uint32_t app_vision_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
static inline uint8_t app_rgb565_to_gray(uint16_t pixel)
{
    uint32_t r = (pixel >> 11) & 0x1F;
    uint32_t g = (pixel >> 5) & 0x3F;
    uint32_t b = pixel & 0x1F;
    r = (r * 255U) / 31U;
    g = (g * 255U) / 63U;
    b = (b * 255U) / 31U;
    return (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
}
static void app_vision_snapshot(app_vision_frame_info_t *meta_out,
                                app_vision_gray_slot_t *slot_out,
                                uint32_t *overwrite_out)
{
    taskENTER_CRITICAL(&s_vision_mux);
    *meta_out = s_latest_frame;
    memcpy(slot_out, &s_gray_slot, sizeof(app_vision_gray_slot_t));
    *overwrite_out = s_submit_overwrite;
    taskEXIT_CRITICAL(&s_vision_mux);
}
static void app_vision_store_result(const app_vision_result_t *result)
{
    taskENTER_CRITICAL(&s_vision_mux);
    s_latest_result = *result;
    taskEXIT_CRITICAL(&s_vision_mux);
}
bool app_vision_get_latest_result(app_vision_result_t *out)
{
    if (out == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&s_vision_mux);
    *out = s_latest_result;
    taskEXIT_CRITICAL(&s_vision_mux);
    return out->valid;
}
static void app_vision_set_wait_text(uint32_t heartbeat)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "tag: wait #%lu", (unsigned long)heartbeat);
    app_ui_set_vision_text(buf);
}
static void app_vision_update_result(const app_vision_gray_slot_t *slot,
                                     const app_apriltag_result_t *tag,
                                     uint32_t detect_ms,
                                     uint16_t *stable_count,
                                     uint16_t *lost_count,
                                     uint16_t *last_tag_id)
{
    app_vision_result_t result = {0};
    if (tag != NULL && tag->valid) {
        if (*last_tag_id == tag->id) {
            if (*stable_count < UINT16_MAX) {
                (*stable_count)++;
            }
        } else {
            *stable_count = 1;
            *last_tag_id = tag->id;
        }
        *lost_count = 0;
        result.valid = true;
        result.tag_id = tag->id;
        result.hamming = tag->hamming;
        result.rotation = tag->rotation;
        result.threshold = tag->threshold;
        result.border_dark_pct = tag->border_dark_pct;
        result.center_x = tag->center_x;
        result.center_y = tag->center_y;
        result.area = tag->area;
        result.bbox_x = tag->bbox_x;
        result.bbox_y = tag->bbox_y;
        result.bbox_w = tag->bbox_w;
        result.bbox_h = tag->bbox_h;
        result.corner_tl_x = tag->corner_tl_x;
        result.corner_tl_y = tag->corner_tl_y;
        result.corner_tr_x = tag->corner_tr_x;
        result.corner_tr_y = tag->corner_tr_y;
        result.corner_br_x = tag->corner_br_x;
        result.corner_br_y = tag->corner_br_y;
        result.corner_bl_x = tag->corner_bl_x;
        result.corner_bl_y = tag->corner_bl_y;
        result.edge_px_avg = tag->edge_px_avg;
        result.top_edge_angle_deg = tag->top_edge_angle_deg;
        result.frame_seq = slot->info.seq;
        result.detect_ms = detect_ms;
        result.stable_count = *stable_count;
        result.lost_count = *lost_count;
        app_vision_store_result(&result);
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "id:%u hm:%u st:%u e:%.1f ang:%d",
                 (unsigned)result.tag_id,
                 (unsigned)result.hamming,
                 (unsigned)result.stable_count,
                 (double)result.edge_px_avg,
                 (int)result.top_edge_angle_deg);
        app_ui_set_vision_text(buf);
        ESP_LOGI(TAG,
                 "tag seq=%lu id=%u hm=%u rot=%u th=%u border=%u area=%ld center=(%ld,%ld) bbox=(%ld,%ld,%ld,%ld) edge=%.1f ang=%.1f stable=%u detect=%lums",
                 (unsigned long)result.frame_seq,
                 (unsigned)result.tag_id,
                 (unsigned)result.hamming,
                 (unsigned)result.rotation,
                 (unsigned)result.threshold,
                 (unsigned)result.border_dark_pct,
                 (long)result.area,
                 (long)result.center_x,
                 (long)result.center_y,
                 (long)result.bbox_x,
                 (long)result.bbox_y,
                 (long)result.bbox_w,
                 (long)result.bbox_h,
                 (double)result.edge_px_avg,
                 (double)result.top_edge_angle_deg,
                 (unsigned)result.stable_count,
                 (unsigned long)result.detect_ms);
        return;
    }
    if (*lost_count < UINT16_MAX) {
        (*lost_count)++;
    }
    if ((*lost_count < VISION_LOST_RESET_FRAMES) && (*stable_count > VISION_STABLE_DECAY_ON_LOST)) {
        *stable_count = (uint16_t)(*stable_count - VISION_STABLE_DECAY_ON_LOST);
    } else if (*lost_count >= VISION_LOST_RESET_FRAMES) {
        *stable_count = 0;
        *last_tag_id = 0;
    }
    result.valid = false;
    result.frame_seq = slot->info.seq;
    result.detect_ms = detect_ms;
    result.stable_count = *stable_count;
    result.lost_count = *lost_count;
    app_vision_store_result(&result);
    char buf[96];
    snprintf(buf,
             sizeof(buf),
             "tag: lost #%u st:%u t:%lums",
             (unsigned)*lost_count,
             (unsigned)*stable_count,
             (unsigned long)detect_ms);
    app_ui_set_vision_text(buf);
}
static void app_vision_task(void *arg)
{
    (void)arg;
    uint32_t heartbeat = 0;
    uint32_t last_seq = 0;
    uint32_t last_overwrite = 0;
    TickType_t last_heartbeat_tick = xTaskGetTickCount();
    uint16_t stable_count = 0;
    uint16_t lost_count = 0;
    uint16_t last_tag_id = 0;
    app_ui_set_vision_text("tag: wait frame");
    while (1) {
        app_vision_frame_info_t meta;
        uint32_t overwrite = 0;
        app_vision_snapshot(&meta, &s_task_slot, &overwrite);
        if (s_task_slot.info.seq != 0 && s_task_slot.info.seq != last_seq) {
            int64_t start_us = esp_timer_get_time();
            app_apriltag_result_t tag = {0};
            bool found = app_apriltag_detect_tag36h11(s_task_slot.gray,
                                                      s_task_slot.info.gray_width,
                                                      s_task_slot.info.gray_height,
                                                      &tag);
            uint32_t detect_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL);
            if (found) {
                app_vision_update_result(&s_task_slot,
                                         &tag,
                                         detect_ms,
                                         &stable_count,
                                         &lost_count,
                                         &last_tag_id);
            } else {
                app_vision_update_result(&s_task_slot,
                                         NULL,
                                         detect_ms,
                                         &stable_count,
                                         &lost_count,
                                         &last_tag_id);
            }
            if (last_seq != 0 && s_task_slot.info.seq > (last_seq + 1)) {
                ESP_LOGW(TAG,
                         "vision frame jump: last=%lu now=%lu lost=%lu overwrite=%lu",
                         (unsigned long)last_seq,
                         (unsigned long)s_task_slot.info.seq,
                         (unsigned long)(s_task_slot.info.seq - last_seq - 1),
                         (unsigned long)(overwrite - last_overwrite));
            }
            last_seq = s_task_slot.info.seq;
            last_overwrite = overwrite;
            last_heartbeat_tick = xTaskGetTickCount();
        } else if (meta.seq != 0 && meta.seq != last_seq) {
            char buf[64];
            snprintf(buf,
                     sizeof(buf),
                     "frame#%lu %lux%lu",
                     (unsigned long)meta.seq,
                     (unsigned long)meta.width,
                     (unsigned long)meta.height);
            app_ui_set_vision_text(buf);
            last_seq = meta.seq;
            last_heartbeat_tick = xTaskGetTickCount();
        } else {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_heartbeat_tick) >= pdMS_TO_TICKS(VISION_HEARTBEAT_MS)) {
                heartbeat++;
                app_vision_set_wait_text(heartbeat);
                last_heartbeat_tick = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(VISION_POLL_PERIOD_MS));
    }
}
esp_err_t app_vision_init(void)
{
    if (s_vision_inited) {
        return ESP_OK;
    }
    esp_err_t ret = app_apriltag_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "app_apriltag_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    taskENTER_CRITICAL(&s_vision_mux);
    memset(&s_latest_frame, 0, sizeof(s_latest_frame));
    memset(&s_gray_slot, 0, sizeof(s_gray_slot));
    memset(&s_task_slot, 0, sizeof(s_task_slot));
    memset(&s_submit_slot, 0, sizeof(s_submit_slot));
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    s_submit_seq = 0;
    s_submit_overwrite = 0;
    s_first_submit_logged = false;
    taskEXIT_CRITICAL(&s_vision_mux);
    ESP_LOGI(TAG, "vision init done, gray=%dx%d", VISION_GRAY_WIDTH, VISION_GRAY_HEIGHT);
    s_vision_inited = true;
    return ESP_OK;
}
esp_err_t app_vision_start(void)
{
    if (!s_vision_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_vision_task != NULL) {
        return ESP_OK;
    }
    BaseType_t ret = xTaskCreatePinnedToCore(app_vision_task,
                                             "app_vision",
                                             VISION_TASK_STACK_SIZE,
                                             NULL,
                                             VISION_TASK_PRIORITY,
                                             &s_vision_task,
                                             VISION_TASK_CORE_ID);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create vision task failed");
        s_vision_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "vision task started");
    return ESP_OK;
}
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
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
    memset(&s_submit_slot, 0, sizeof(s_submit_slot));
    s_submit_slot.info.src_width = width;
    s_submit_slot.info.src_height = height;
    s_submit_slot.info.gray_width = VISION_GRAY_WIDTH;
    s_submit_slot.info.gray_height = VISION_GRAY_HEIGHT;
    s_submit_slot.info.gray_len = VISION_GRAY_BUF_SIZE;
    const uint16_t *src = (const uint16_t *)rgb565;
    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {
        uint32_t sy = (gy * height) / VISION_GRAY_HEIGHT;
        if (sy >= height) {
            sy = height - 1;
        }
        for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {
            uint32_t sx = (gx * width) / VISION_GRAY_WIDTH;
            if (sx >= width) {
                sx = width - 1;
            }
            uint16_t pixel = src[sy * width + sx];
            s_submit_slot.gray[gy * VISION_GRAY_WIDTH + gx] = app_rgb565_to_gray(pixel);
        }
    }
    taskENTER_CRITICAL(&s_vision_mux);
    if (s_gray_slot.info.seq != 0) {
        s_submit_overwrite++;
    }
    s_submit_seq++;
    s_latest_frame.width = width;
    s_latest_frame.height = height;
    s_latest_frame.len = len;
    s_latest_frame.seq = s_submit_seq;
    s_latest_frame.tick_ms = app_vision_now_ms();
    s_submit_slot.info.seq = s_submit_seq;
    s_submit_slot.info.tick_ms = s_latest_frame.tick_ms;
    memcpy(&s_gray_slot, &s_submit_slot, sizeof(app_vision_gray_slot_t));
    taskEXIT_CRITICAL(&s_vision_mux);
    if (!s_first_submit_logged) {
        ESP_LOGI(TAG,
                 "first gray frame ready: src=%lux%lu gray=%dx%d len=%lu",
                 (unsigned long)width,
                 (unsigned long)height,
                 VISION_GRAY_WIDTH,
                 VISION_GRAY_HEIGHT,
                 (unsigned long)len);
        s_first_submit_logged = true;
    }
    return ESP_OK;
}
