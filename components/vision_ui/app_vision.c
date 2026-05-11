/* 实现说明：AprilTag 检测在后台任务中运行，不放在摄像头回调里。 */
/*
 * app_vision.c - 视觉识别任务调度模块
 *
 * 这个文件位于 app_camera.c 和 app_apriltag.c 之间：
 * - app_camera.c 提交 RGB565 帧；
 * - 本文件把 RGB565 缩小/转成灰度图；
 * - 后台 FreeRTOS 任务调用 app_apriltag_detect_tag36h11()；
 * - 把识别结果缓存起来，并更新 UI 上的视觉文本。
 *
 * 设计重点是“不要卡住摄像头预览和 LVGL”：提交帧时只做轻量拷贝/转换，真正耗时的 AprilTag 检测放到后台任务里跑。
 */

#include "app_vision.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app_apriltag.h"
#include "app_ai_capture.h"
#include "app_ui.h"

/* -------------------------------------------------------------------------- */
/* 视觉任务配置                                                   */
/* -------------------------------------------------------------------------- */

#define VISION_TASK_STACK_SIZE         (16 * 1024)
#define VISION_TASK_PRIORITY           4
#define VISION_TASK_CORE_ID            1
#define VISION_POLL_PERIOD_MS          25
#define VISION_HEARTBEAT_MS            1000
#define VISION_GRAY_WIDTH              320
#define VISION_GRAY_HEIGHT             240
#define VISION_GRAY_BUF_SIZE           (VISION_GRAY_WIDTH * VISION_GRAY_HEIGHT)
#define VISION_LOST_RESET_FRAMES       2U
#define VISION_STABLE_DECAY_ON_LOST    1U
#define VISION_FRAME_JUMP_LOG_MS       1000U
static const char *TAG = "app_vision";

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    app_vision_gray_frame_info_t info;    /* 灰度帧的尺寸、序号和时间戳信息。 */
    uint8_t gray[VISION_GRAY_BUF_SIZE];   /* 供 AprilTag 检测使用的灰度图数据。 */
} app_vision_gray_slot_t;
static TaskHandle_t s_vision_task = NULL;
static bool s_vision_inited = false;
static portMUX_TYPE s_vision_mux = portMUX_INITIALIZER_UNLOCKED;
static app_vision_frame_info_t s_latest_frame = {0};
static app_vision_gray_slot_t s_slot_a = {0};
static app_vision_gray_slot_t s_slot_b = {0};
static app_vision_gray_slot_t s_slot_c = {0};
/* 三个槽位各司其职：pending 等待检测任务领取，detect 正在被检测，write 供摄像头回调写入新灰度帧。 */
static app_vision_gray_slot_t *s_pending_slot = &s_slot_a;
static app_vision_gray_slot_t *s_detect_slot = &s_slot_b;
static app_vision_gray_slot_t *s_write_slot = &s_slot_c;
static app_vision_result_t s_latest_result = {0};
static uint32_t s_submit_seq = 0;
static uint32_t s_submit_overwrite = 0;
static uint32_t s_submit_busy_drop = 0;
static bool s_first_submit_logged = false;
static uint16_t s_sample_x[VISION_GRAY_WIDTH];
static uint16_t s_sample_y[VISION_GRAY_HEIGHT];
static uint32_t s_sample_map_width = 0;
static uint32_t s_sample_map_height = 0;
static uint32_t s_sample_crop_x = 0;
static uint32_t s_sample_crop_y = 0;
static uint32_t s_sample_crop_w = 0;
static uint32_t s_sample_crop_h = 0;

static void app_vision_log_heap(const char *stage)
{
    ESP_LOGI(TAG,
        "%s heap: int8_free=%lu int8_largest=%lu psram_free=%lu psram_largest=%lu",
        stage,
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

/* -------------------------------------------------------------------------- */
/* 时间、颜色和采样映射辅助函数                                         */
/* -------------------------------------------------------------------------- */

/* 获取当前系统毫秒时间，用作帧时间戳。 */
static inline uint32_t app_vision_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/* 将一个 RGB565 像素转换成 8 位灰度值。 */
static inline uint8_t app_rgb565_to_gray(uint16_t pixel)
{
    uint32_t r = (pixel >> 11) & 0x1F;
    uint32_t g = (pixel >> 5) & 0x3F;
    uint32_t b = pixel & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
}
/* 按目标宽高比计算居中裁剪区域。 */
static void app_vision_calc_center_crop(uint32_t src_width,
    uint32_t src_height,
    uint32_t dst_width,
    uint32_t dst_height,
    uint32_t *crop_x,
    uint32_t *crop_y,
    uint32_t *crop_w,
    uint32_t *crop_h)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = src_width;
    uint32_t h = src_height;

    if (src_width == 0U || src_height == 0U || dst_width == 0U || dst_height == 0U)
    {
        w = 0;
        h = 0;
    }
    else if (((uint64_t)src_width * dst_height) > ((uint64_t)src_height * dst_width))
    {
        w = (uint32_t)(((uint64_t)src_height * dst_width) / dst_height);
        if (w == 0U || w > src_width)
        {
            w = src_width;
        }
        x = (src_width - w) / 2U;
    }
    else if (((uint64_t)src_width * dst_height) < ((uint64_t)src_height * dst_width))
    {
        h = (uint32_t)(((uint64_t)src_width * dst_height) / dst_width);
        if (h == 0U || h > src_height)
        {
            h = src_height;
        }
        y = (src_height - h) / 2U;
    }

    if (crop_x) *crop_x = x;
    if (crop_y) *crop_y = y;
    if (crop_w) *crop_w = w;
    if (crop_h) *crop_h = h;
}
/* 为当前输入尺寸预计算灰度图采样坐标表。 */
static void app_vision_prepare_sample_map(uint32_t width, uint32_t height)
{
    if (s_sample_map_width == width && s_sample_map_height == height)
    {
        return;
    }

    uint32_t crop_x = 0;
    uint32_t crop_y = 0;
    uint32_t crop_w = width;
    uint32_t crop_h = height;
    app_vision_calc_center_crop(width,
        height,
        VISION_GRAY_WIDTH,
        VISION_GRAY_HEIGHT,
        &crop_x,
        &crop_y,
        &crop_w,
        &crop_h);

    for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {
        uint32_t sx = crop_x +
            (uint32_t)((((uint64_t)(2U * gx + 1U)) * crop_w) / (2U * VISION_GRAY_WIDTH));
        if (sx >= width)
        {
            sx = width - 1U;
        }
        s_sample_x[gx] = (uint16_t)sx;
    }

    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {
        uint32_t sy = crop_y +
            (uint32_t)((((uint64_t)(2U * gy + 1U)) * crop_h) / (2U * VISION_GRAY_HEIGHT));
        if (sy >= height)
        {
            sy = height - 1U;
        }
        s_sample_y[gy] = (uint16_t)sy;
    }

    s_sample_map_width = width;
    s_sample_map_height = height;
    s_sample_crop_x = crop_x;
    s_sample_crop_y = crop_y;
    s_sample_crop_w = crop_w;
    s_sample_crop_h = crop_h;
}

/* -------------------------------------------------------------------------- */
/* 共享帧和结果快照                                               */
/* -------------------------------------------------------------------------- */

/* 从提交队列取出一帧灰度图快照给检测任务使用。 */
static app_vision_gray_slot_t *app_vision_take_snapshot(app_vision_frame_info_t *meta_out,
    uint32_t *overwrite_out)
{
    app_vision_gray_slot_t *slot = NULL;

    taskENTER_CRITICAL(&s_vision_mux);
    *meta_out = s_latest_frame;
    *overwrite_out = s_submit_overwrite;
    if (s_pending_slot->info.seq != 0)
    {
        app_vision_gray_slot_t *ready_slot = s_pending_slot;
        s_pending_slot = s_detect_slot;
        s_detect_slot = ready_slot;
        memset(&s_pending_slot->info, 0, sizeof(s_pending_slot->info));
    }
    slot = s_detect_slot;

    taskEXIT_CRITICAL(&s_vision_mux);
    return slot;
}

/* 背压入口：如果还有一帧在排队，就跳过新帧，避免把 CPU 花在马上会被丢弃的灰度转换上。 */
/* 判断当前是否还能接收一帧新图像，避免无意义转换。 */
static bool app_vision_can_accept_frame(void)
{
    bool ready = false;

    taskENTER_CRITICAL(&s_vision_mux);
    ready = (s_pending_slot->info.seq == 0);
    taskEXIT_CRITICAL(&s_vision_mux);
    return ready;
}

/* 记录一次因为检测任务来不及消费而跳过的帧。 */
static void app_vision_mark_busy_drop(void)
{
    taskENTER_CRITICAL(&s_vision_mux);
    s_submit_busy_drop++;
    taskEXIT_CRITICAL(&s_vision_mux);
}

/* 在临界区内保存最新 AprilTag 检测结果。 */
static void app_vision_store_result(const app_vision_result_t *result)
{

    taskENTER_CRITICAL(&s_vision_mux);
    s_latest_result = *result;

    taskEXIT_CRITICAL(&s_vision_mux);
}
/* 读取最新检测结果快照，返回值表示当前结果是否有效。 */
bool app_vision_get_latest_result(app_vision_result_t *out)
{
    if (out == NULL)
    {
        return false;
    }

    taskENTER_CRITICAL(&s_vision_mux);
    *out = s_latest_result;

    taskEXIT_CRITICAL(&s_vision_mux);
    return out->valid;
}

/* -------------------------------------------------------------------------- */
/* 检测结果处理                                                   */
/* -------------------------------------------------------------------------- */

/* 在没有新检测结果时更新等待帧提示。 */
static void app_vision_set_wait_text(uint32_t heartbeat)
{
    if (app_ai_capture_is_active())
    {
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "tag: wait #%lu", (unsigned long)heartbeat);
    app_ui_set_vision_text(buf);
}
/* 把灰度裁剪图元数据填入检测结果。 */
static void app_vision_fill_result_geometry(app_vision_result_t *result,
    const app_vision_gray_frame_info_t *info)
{
    if (result == NULL || info == NULL)
    {
        return;
    }
    result->src_width = info->src_width;
    result->src_height = info->src_height;
    result->crop_x = info->crop_x;
    result->crop_y = info->crop_y;
    result->crop_w = info->crop_w;
    result->crop_h = info->crop_h;
    result->gray_width = info->gray_width;
    result->gray_height = info->gray_height;
}
/* 根据检测输出更新稳定计数、丢失计数、最新结果和 UI 文本。 */
static void app_vision_update_result(const app_vision_gray_slot_t *slot,
    const app_apriltag_result_t *tag,
    uint32_t detect_ms,
    uint16_t *stable_count,
    uint16_t *lost_count,
    uint16_t *last_tag_id)
{
    app_vision_result_t result = {0};
    app_vision_fill_result_geometry(&result, &slot->info);
    if (tag != NULL && tag->valid)
    {
        if (*last_tag_id == tag->id)
        {
            if (*stable_count < UINT16_MAX)
            {
                (*stable_count)++;
            }
        }
        else
        {
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
        if (!app_ai_capture_is_active())
        {
            app_ui_set_vision_text(buf);
        }
        ESP_LOGD(TAG,
            "tag seq=%lu id=%u hm=%u rot=%u th=%u border=%u area=%ld center=(%ld,%ld) bbox=(%ld,%ld,%ld,%ld) edge=%.1f ang=%.1f stable=%u detect=%lums crop=(%lu,%lu,%lu,%lu)",
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
            (unsigned long)result.detect_ms,
            (unsigned long)result.crop_x,
            (unsigned long)result.crop_y,
            (unsigned long)result.crop_w,
            (unsigned long)result.crop_h);
        return;
    }
    if (*lost_count < UINT16_MAX)
    {
        (*lost_count)++;
    }
    if ((*lost_count < VISION_LOST_RESET_FRAMES) && (*stable_count > VISION_STABLE_DECAY_ON_LOST))
    {
        *stable_count = (uint16_t)(*stable_count - VISION_STABLE_DECAY_ON_LOST);
    }
    else if (*lost_count >= VISION_LOST_RESET_FRAMES)
    {
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
    if (!app_ai_capture_is_active())
    {
        app_ui_set_vision_text(buf);
    }
}

/* -------------------------------------------------------------------------- */
/* 任务和公开接口                                                         */
/* -------------------------------------------------------------------------- */

/* 视觉后台任务，领取灰度帧并运行 AprilTag 检测。 */
static void app_vision_task(void *arg)
{
    (void)arg;
    uint32_t heartbeat = 0;
    uint32_t last_seq = 0;
    uint32_t last_overwrite = 0;
    uint32_t last_busy_drop = 0;
    uint32_t jump_lost_accum = 0;
    uint32_t jump_overwrite_accum = 0;
    TickType_t last_heartbeat_tick = xTaskGetTickCount();
    TickType_t last_jump_log_tick = 0;
    uint16_t stable_count = 0;
    uint16_t lost_count = 0;
    uint16_t last_tag_id = 0;

    if (!app_ai_capture_is_active())
    {
        app_ui_set_vision_text("tag: wait frame");
    }

    while (1) {
        app_vision_frame_info_t meta;
        uint32_t overwrite = 0;
        uint32_t busy_drop = 0;
        app_vision_gray_slot_t *slot = app_vision_take_snapshot(&meta, &overwrite);
        taskENTER_CRITICAL(&s_vision_mux);
        busy_drop = s_submit_busy_drop;
        taskEXIT_CRITICAL(&s_vision_mux);
        if (slot->info.seq != 0 && slot->info.seq != last_seq)
        {
            int64_t start_us = esp_timer_get_time();
            app_apriltag_result_t tag = {0};
            bool found = app_apriltag_detect_tag36h11(slot->gray,
                slot->info.gray_width,
                slot->info.gray_height,
                &tag);
            uint32_t detect_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL);
            if (found)
            {
                app_vision_update_result(slot,
                    &tag,
                    detect_ms,
                    &stable_count,
                    &lost_count,
                    &last_tag_id);
            }
            else
            {
                app_vision_update_result(slot,
                    NULL,
                    detect_ms,
                    &stable_count,
                    &lost_count,
                    &last_tag_id);
            }
            if (last_seq != 0 && slot->info.seq > (last_seq + 1))
            {
                jump_lost_accum += (slot->info.seq - last_seq - 1);
                jump_overwrite_accum += (overwrite - last_overwrite);
                TickType_t now_tick = xTaskGetTickCount();
                if ((last_jump_log_tick == 0) ||
                    ((now_tick - last_jump_log_tick) >= pdMS_TO_TICKS(VISION_FRAME_JUMP_LOG_MS)))
                {
                    ESP_LOGW(TAG,
                        "vision frame jump: last=%lu now=%lu lost_total=%lu overwrite_total=%lu",
                        (unsigned long)last_seq,
                        (unsigned long)slot->info.seq,
                        (unsigned long)jump_lost_accum,
                        (unsigned long)jump_overwrite_accum);
                    jump_lost_accum = 0;
                    jump_overwrite_accum = 0;
                    last_jump_log_tick = now_tick;
                }
            }
            last_seq = slot->info.seq;
            last_overwrite = overwrite;
            last_busy_drop = busy_drop;
            last_heartbeat_tick = xTaskGetTickCount();
        }
        else if (meta.seq != 0 && meta.seq != last_seq)
        {
            char buf[64];
            snprintf(buf,
                sizeof(buf),
                "frame#%lu %lux%lu",
                (unsigned long)meta.seq,
                (unsigned long)meta.width,
                (unsigned long)meta.height);
            if (!app_ai_capture_is_active())
            {
                app_ui_set_vision_text(buf);
            }
            last_seq = meta.seq;
            last_busy_drop = busy_drop;
            last_heartbeat_tick = xTaskGetTickCount();
        }
        else
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_heartbeat_tick) >= pdMS_TO_TICKS(VISION_HEARTBEAT_MS))
            {
                heartbeat++;
                app_vision_set_wait_text(heartbeat);
                if (busy_drop != last_busy_drop)
                {
                    ESP_LOGD(TAG,
                        "vision busy drop: total=%lu delta=%lu",
                        (unsigned long)busy_drop,
                        (unsigned long)(busy_drop - last_busy_drop));
                    last_busy_drop = busy_drop;
                }
                last_heartbeat_tick = now;
            }
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VISION_POLL_PERIOD_MS));
    }
}
/* 初始化 AprilTag 检测器、三缓冲槽位和统计计数。 */
esp_err_t app_vision_init(void)
{
    if (s_vision_inited)
    {
        return ESP_OK;
    }
    esp_err_t ret = app_apriltag_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "app_apriltag_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    taskENTER_CRITICAL(&s_vision_mux);
    memset(&s_latest_frame, 0, sizeof(s_latest_frame));
    memset(&s_slot_a, 0, sizeof(s_slot_a));
    memset(&s_slot_b, 0, sizeof(s_slot_b));
    memset(&s_slot_c, 0, sizeof(s_slot_c));
    s_pending_slot = &s_slot_a;
    s_detect_slot = &s_slot_b;
    s_write_slot = &s_slot_c;
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    s_submit_seq = 0;
    s_submit_overwrite = 0;
    s_submit_busy_drop = 0;
    s_first_submit_logged = false;
    s_sample_map_width = 0;
    s_sample_map_height = 0;
    s_sample_crop_x = 0;
    s_sample_crop_y = 0;
    s_sample_crop_w = 0;
    s_sample_crop_h = 0;

    taskEXIT_CRITICAL(&s_vision_mux);
    ESP_LOGI(TAG, "vision init done, gray=%dx%d", VISION_GRAY_WIDTH, VISION_GRAY_HEIGHT);
    s_vision_inited = true;
    return ESP_OK;
}
/* 创建视觉检测 FreeRTOS 任务，重复启动会直接成功返回。 */
esp_err_t app_vision_start(void)
{
    if (!s_vision_inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_vision_task != NULL)
    {
        return ESP_OK;
    }

    app_vision_log_heap("before vision task start");

#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(app_vision_task,
        "app_vision",
        VISION_TASK_STACK_SIZE,
        NULL,
        VISION_TASK_PRIORITY,
        &s_vision_task,
        VISION_TASK_CORE_ID,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        ESP_LOGW(TAG, "create vision task with PSRAM stack failed, try internal stack");
        ret = xTaskCreatePinnedToCore(app_vision_task,
            "app_vision",
            VISION_TASK_STACK_SIZE,
            NULL,
            VISION_TASK_PRIORITY,
            &s_vision_task,
            VISION_TASK_CORE_ID);
    }
#else
    BaseType_t ret = xTaskCreatePinnedToCore(app_vision_task,
        "app_vision",
        VISION_TASK_STACK_SIZE,
        NULL,
        VISION_TASK_PRIORITY,
        &s_vision_task,
        VISION_TASK_CORE_ID);
#endif
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "create vision task failed");
        app_vision_log_heap("vision task create failed");
        s_vision_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "vision task started");
    return ESP_OK;
}
/* 摄像头回调侧提交帧：转换为灰度采样图后交给后台检测任务。 */
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
    uint32_t width,
    uint32_t height,
    size_t len)
{
    if (rgb565 == NULL || width == 0 || height == 0)
    {

        return ESP_ERR_INVALID_ARG;
    }
    const size_t min_len = (size_t)width * (size_t)height * 2U;
    if (len < min_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!app_vision_can_accept_frame())
    {
        app_vision_mark_busy_drop();
        return ESP_OK;
    }

    app_vision_gray_slot_t *write_slot = s_write_slot;
    if (write_slot == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&write_slot->info, 0, sizeof(write_slot->info));
    write_slot->info.src_width = width;
    write_slot->info.src_height = height;
    write_slot->info.gray_width = VISION_GRAY_WIDTH;
    write_slot->info.gray_height = VISION_GRAY_HEIGHT;
    write_slot->info.gray_len = VISION_GRAY_BUF_SIZE;
    app_vision_prepare_sample_map(width, height);
    write_slot->info.crop_x = s_sample_crop_x;
    write_slot->info.crop_y = s_sample_crop_y;
    write_slot->info.crop_w = s_sample_crop_w;
    write_slot->info.crop_h = s_sample_crop_h;
    const uint16_t *src = (const uint16_t *)rgb565;
    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {
        const uint16_t *src_row = src + (size_t)s_sample_y[gy] * width;
        uint8_t *dst_row = &write_slot->gray[gy * VISION_GRAY_WIDTH];
        for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {
            uint16_t pixel = src_row[s_sample_x[gx]];
            dst_row[gx] = app_rgb565_to_gray(pixel);
        }
    }

    taskENTER_CRITICAL(&s_vision_mux);
    if (s_pending_slot->info.seq != 0)
    {
        s_submit_overwrite++;
    }
    s_submit_seq++;
    s_latest_frame.width = width;
    s_latest_frame.height = height;
    s_latest_frame.len = len;
    s_latest_frame.seq = s_submit_seq;
    s_latest_frame.tick_ms = app_vision_now_ms();
    write_slot->info.seq = s_submit_seq;
    write_slot->info.tick_ms = s_latest_frame.tick_ms;
    app_vision_gray_slot_t *old_pending_slot = s_pending_slot;
    s_pending_slot = write_slot;
    s_write_slot = old_pending_slot;
    memset(&s_write_slot->info, 0, sizeof(s_write_slot->info));

    taskEXIT_CRITICAL(&s_vision_mux);
    if (s_vision_task != NULL)
    {
        xTaskNotifyGive(s_vision_task);
    }

    if (!s_first_submit_logged)
    {
        ESP_LOGD(TAG,
            "first gray frame ready: src=%lux%lu crop=(%lu,%lu,%lu,%lu) gray=%dx%d len=%lu",
            (unsigned long)width,
            (unsigned long)height,
            (unsigned long)s_sample_crop_x,
            (unsigned long)s_sample_crop_y,
            (unsigned long)s_sample_crop_w,
            (unsigned long)s_sample_crop_h,
            VISION_GRAY_WIDTH,
            VISION_GRAY_HEIGHT,
            (unsigned long)len);
        s_first_submit_logged = true;
    }
    return ESP_OK;
}
