#include "app_vision.h"
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
#include "app_image_utils.h"

// 视觉管线：从相机 RGB565 帧抽样生成 320x240 灰度图，后台检测 AprilTag 并发布最新结果。

#define VISION_TASK_STACK_SIZE         (16 * 1024)
#define VISION_TASK_PRIORITY           4
#define VISION_TASK_CORE_ID            0
#define VISION_POLL_PERIOD_MS          25
#define VISION_SLOW_DETECT_MS          1000U
#define VISION_GRAY_WIDTH              320
#define VISION_GRAY_HEIGHT             240
#define VISION_GRAY_BUF_SIZE           (VISION_GRAY_WIDTH * VISION_GRAY_HEIGHT)
#define VISION_LOST_RESET_FRAMES       2U
#define VISION_STABLE_DECAY_ON_LOST    1U
static const char *TAG = "app_vision";
typedef struct {
    app_vision_gray_frame_info_t info;  // 原图、裁剪区域和帧序号。
    uint8_t gray[VISION_GRAY_BUF_SIZE]; // 固定 320x240 灰度检测图。
} app_vision_gray_slot_t;
static TaskHandle_t s_vision_task = NULL;
static bool s_vision_inited = false;
static portMUX_TYPE s_vision_mux = portMUX_INITIALIZER_UNLOCKED;
static app_vision_frame_info_t s_latest_frame = {0};
static app_vision_gray_slot_t s_slot_a = {0};
static app_vision_gray_slot_t s_slot_b = {0};
static app_vision_gray_slot_t s_slot_c = {0};
// 三槽轮转：pending 给提交者发布，detect 给检测任务读取，write 给下一帧写入。
static app_vision_gray_slot_t *s_pending_slot = &s_slot_a;
static app_vision_gray_slot_t *s_detect_slot = &s_slot_b;
static app_vision_gray_slot_t *s_write_slot = &s_slot_c;
static app_vision_result_t s_latest_result = {0};
static uint32_t s_submit_seq = 0;
static uint16_t s_sample_x[VISION_GRAY_WIDTH];
static uint16_t s_sample_y[VISION_GRAY_HEIGHT];
static uint32_t s_sample_map_width = 0;
static uint32_t s_sample_map_height = 0;
static uint32_t s_sample_crop_x = 0;
static uint32_t s_sample_crop_y = 0;
static uint32_t s_sample_crop_w = 0;
static uint32_t s_sample_crop_h = 0;

/* ---------- 采样映射与三槽所有权 ---------- */

static inline uint32_t app_vision_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
// 输入分辨率不变时复用采样映射，减少每帧除法开销。
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
    app_image_calc_center_crop(width,
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
// 检测任务取走最新 pending 槽，并把旧 detect 槽换回 pending。
static app_vision_gray_slot_t *app_vision_take_snapshot(app_vision_frame_info_t *meta_out)
{
    app_vision_gray_slot_t *slot = NULL;
    taskENTER_CRITICAL(&s_vision_mux);
    *meta_out = s_latest_frame;
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
// pending 槽未被检测任务取走时拒收新帧，调用方记录 busy drop。
static bool app_vision_can_accept_frame(void)
{
    bool ready = false;
    taskENTER_CRITICAL(&s_vision_mux);
    ready = (s_pending_slot->info.seq == 0);
    taskEXIT_CRITICAL(&s_vision_mux);
    return ready;
}
static void app_vision_store_result(const app_vision_result_t *result)
{
    taskENTER_CRITICAL(&s_vision_mux);
    s_latest_result = *result;
    taskEXIT_CRITICAL(&s_vision_mux);
}
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
// 将灰度检测区域和原图尺寸写入结果，供 UI 映射回屏幕坐标。
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
// 根据检测成败更新稳定/丢失计数、最新结果和 HUD 文案。
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
}
// 检测任务：等待新灰度帧，调用 AprilTag 检测器并记录跳帧/覆盖情况。
/* ---------- 后台检测任务 ---------- */

static void app_vision_task(void *arg)
{
    (void)arg;
    uint32_t last_seq = 0;
    uint32_t last_slow_log_ms = 0;
    uint16_t stable_count = 0;
    uint16_t lost_count = 0;
    uint16_t last_tag_id = 0;
    while (1) {
        app_vision_frame_info_t meta;
        app_vision_gray_slot_t *slot = app_vision_take_snapshot(&meta);
        if (slot->info.seq != 0 && slot->info.seq != last_seq)
        {
            int64_t start_us = esp_timer_get_time();
            app_apriltag_result_t tag = {0};
            bool found = app_apriltag_detect_tag36h11(slot->gray,
                slot->info.gray_width,
                slot->info.gray_height,
                &tag);
            uint32_t detect_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL);
            const uint32_t now_ms = app_vision_now_ms();
            if (detect_ms >= VISION_SLOW_DETECT_MS &&
                now_ms - last_slow_log_ms >= VISION_SLOW_DETECT_MS)
            {
                ESP_LOGW(TAG,
                    "slow AprilTag frame: %lums seq=%lu",
                    (unsigned long)detect_ms,
                    (unsigned long)slot->info.seq);
                last_slow_log_ms = now_ms;
            }
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
            last_seq = slot->info.seq;
        }
        else if (meta.seq != 0 && meta.seq != last_seq)
        {
            last_seq = meta.seq;
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VISION_POLL_PERIOD_MS));
    }
}
// 初始化 AprilTag 检测器和三槽缓冲状态。
/* ---------- 公共生命周期与帧提交 ---------- */

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
    s_sample_map_width = 0;
    s_sample_map_height = 0;
    s_sample_crop_x = 0;
    s_sample_crop_y = 0;
    s_sample_crop_w = 0;
    s_sample_crop_h = 0;
    taskEXIT_CRITICAL(&s_vision_mux);
    s_vision_inited = true;
    return ESP_OK;
}
// 启动视觉检测任务，优先使用 PSRAM 栈。
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
        s_vision_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}
// 相机回调提交 RGB565 帧：居中裁剪、下采样并转换为灰度 pending 槽。
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
    // 最近邻采样并转灰度，保留原图到灰度图的裁剪映射参数。
    const uint16_t *src = (const uint16_t *)rgb565;
    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {
        const uint16_t *src_row = src + (size_t)s_sample_y[gy] * width;
        uint8_t *dst_row = &write_slot->gray[gy * VISION_GRAY_WIDTH];
        for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {
            uint16_t pixel = src_row[s_sample_x[gx]];
            dst_row[gx] = app_image_rgb565_to_gray(pixel);
        }
    }
    taskENTER_CRITICAL(&s_vision_mux);
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
    return ESP_OK;
}
