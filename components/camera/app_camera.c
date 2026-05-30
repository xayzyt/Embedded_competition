#include "app_camera.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "app_video.h"
#include "app_ai_capture.h"
#include "app_drone_ai.h"
#include "app_vision.h"
#include "app_camera_route.h"
#if SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif

// 摄像头预览模块：V4L2 USERPTR 取帧，PPA/CPU 缩放到固定 LVGL canvas，
// 同时按路由把抽样帧提交给 AI、AprilTag 和抓图保存。

#define CAMERA_NUM_BUFS          4
#define STAGE_NUM_BUFS           5
#define UI_CANVAS_NUM_BUFS       3
#define ALIGN_UP(num, align)     (((num) + ((align) - 1)) & ~((align) - 1))
#define DISPLAY_TASK_STACK_SIZE  (6 * 1024)
#define DISPLAY_TASK_PRIORITY    7
#define DISPLAY_LOCK_TIMEOUT_MS  30
#define DISPLAY_CREATE_LOCK_TIMEOUT_MS 200
#define PREVIEW_PREFERRED_WIDTH  1024U
#define PREVIEW_PREFERRED_HEIGHT 600U
#define PREVIEW_WARMUP_DROP_FRAMES      3U
#define PREVIEW_BAD_SAMPLE_COLS         16U
#define PREVIEW_BAD_SAMPLE_ROWS         12U
#define PREVIEW_BAD_CYAN_PERCENT        88U
#define PREVIEW_BAD_SOLID_PERCENT       98U
#define PREVIEW_BAD_CAST_PERCENT        78U
#define PREVIEW_BAD_DETAIL_LOG_LIMIT    12U
#define PREVIEW_BAD_HOLD_FRAMES         3U
#define RECOGNITION_CAMERA_PROFILE_ENABLE 0
#define RECOGNITION_CAMERA_EXPOSURE_US      12000U
#define RECOGNITION_CAMERA_GAIN_PERCENT     70U
#define CAMERA_BUF_CAPS          (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED)
#define DISPLAY_BUF_CAPS         (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA)
static const char *TAG = "app_camera";
static bool s_camera_inited = false;
static volatile bool s_preview_running = false;
static volatile bool s_camera_paused = false;
static int s_video_fd = -1;
static lv_obj_t *s_camera_canvas = NULL;
static size_t s_cache_line_size = 0;
static uint8_t *s_cam_buf[CAMERA_NUM_BUFS] = {0};
static size_t s_cam_buf_size = 0;
// stage buffer 是相机回调和 LVGL 显示任务之间的轻量多缓冲队列。
static uint8_t *s_stage_buf[STAGE_NUM_BUFS] = {0};
static uint8_t *s_ui_canvas_buf[UI_CANVAS_NUM_BUFS] = {0};
static uint8_t s_ui_canvas_active_index = 0;
static uint32_t s_disp_buf_size = 0;
static TaskHandle_t s_display_task_handle = NULL;
static portMUX_TYPE s_display_mux = portMUX_INITIALIZER_UNLOCKED;
typedef enum {
    DISP_BUF_FREE = 0,
    DISP_BUF_WRITING,
    DISP_BUF_READY,
    DISP_BUF_DISPLAYED,
} disp_buf_state_t;
static volatile int s_displayed_stage_index = -1;
static volatile int s_pending_stage_index = -1;
static uint32_t s_frame_count = 0;
static volatile uint32_t s_display_count = 0;
static bool s_ppa_error_logged = false;
static disp_buf_state_t s_stage_state[STAGE_NUM_BUFS] = {0};
static uint32_t s_stage_letterbox_w[STAGE_NUM_BUFS] = {0};
static uint32_t s_stage_letterbox_h[STAGE_NUM_BUFS] = {0};
static volatile int s_retained_stage_index = -1;
static uint32_t s_stage_drop_count = 0;
static uint32_t s_bad_len_count = 0;
static uint32_t s_bad_preview_count = 0;
static uint32_t s_ppa_guard_count = 0;
static uint32_t s_cpu_fallback_count = 0;
static uint32_t s_raw_bad_count = 0;
static uint32_t s_canvas_bad_count = 0;
static uint32_t s_bad_detail_log_count = 0;
static uint32_t s_warmup_drop_remaining = 0;
static uint32_t s_bad_preview_streak = 0;
#if SOC_PPA_SUPPORTED
static ppa_client_handle_t s_ppa_srm_handle = NULL;
#endif
// 对齐到 cache line 后同步，避免 PSRAM/DMA 缓存一致性问题。
static esp_err_t app_camera_msync_aligned(void *addr, size_t size, int flags)
{
    if (addr == NULL || size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    size_t align = s_cache_line_size ? s_cache_line_size : 64;
    uintptr_t start = (uintptr_t)addr & ~((uintptr_t)align - 1U);
    uintptr_t end = ((uintptr_t)addr + size + align - 1U) & ~((uintptr_t)align - 1U);
    return esp_cache_msync((void *)start, end - start, flags);
}
static inline esp_err_t app_camera_msync_m2c(const void *addr, size_t size)
{
    return app_camera_msync_aligned((void *)addr, size,
        ESP_CACHE_MSYNC_FLAG_DIR_M2C |
        ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}
static inline esp_err_t app_camera_msync_c2m(void *addr, size_t size)
{
    return app_camera_msync_aligned(addr, size,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M |
        ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}
// 优先按请求能力分配，DMA 不可用时退回 cache-aligned PSRAM。
static void *app_camera_aligned_calloc(size_t size, uint32_t caps, const char *name)
{
    void *ptr = heap_caps_aligned_calloc(s_cache_line_size, 1, size, caps);
    if (ptr == NULL && (caps & MALLOC_CAP_DMA))
    {
        uint32_t fallback_caps = (caps & ~MALLOC_CAP_DMA) | MALLOC_CAP_CACHE_ALIGNED;
        ESP_LOGW(TAG, "alloc %s with DMA caps failed, retry cache-aligned PSRAM", name);
        ptr = heap_caps_aligned_calloc(s_cache_line_size, 1, size, fallback_caps);
    }
    return ptr;
}
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
static void app_camera_canvas_set_buffer(uint8_t *buf)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_canvas_set_buffer(s_camera_canvas, buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_COLOR_FORMAT_RGB565);
#else
    lv_canvas_set_buffer(s_camera_canvas, buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
#endif
}
static inline bool app_camera_canvas_buffers_ready(void)
{
    for (uint8_t i = 0; i < UI_CANVAS_NUM_BUFS; i++) {
        if (s_ui_canvas_buf[i] == NULL)
        {
            return false;
        }
    }
    return true;
}
// 计算等比例缩放后的预览尺寸，保持画面不拉伸。
static void app_camera_preview_calc_aspect_fit(uint32_t src_w,
    uint32_t src_h,
    uint32_t dst_w,
    uint32_t dst_h,
    uint32_t *out_w,
    uint32_t *out_h)
{
    if (out_w == NULL || out_h == NULL)
    {
        return;
    }
    if (src_w == 0U || src_h == 0U || dst_w == 0U || dst_h == 0U)
    {
        *out_w = 0U;
        *out_h = 0U;
        return;
    }
    const float src_aspect = (float)src_w / (float)src_h;
    const float dst_aspect = (float)dst_w / (float)dst_h;
    if (src_aspect > dst_aspect)
    {
        *out_w = dst_w;
        *out_h = (uint32_t)(dst_w / src_aspect);
    }
    else
    {
        *out_h = dst_h;
        *out_w = (uint32_t)(dst_h * src_aspect);
    }
}
static bool app_camera_frame_length_ok(uint32_t width, uint32_t height, size_t len)
{
    if (width == 0U || height == 0U)
    {
        return false;
    }
    const size_t expected = (size_t)width * (size_t)height * 2U;
    return len >= expected;
}
typedef struct {
    uint32_t sample_count;
    uint32_t cyan_count;
    uint32_t swapped_cyan_count;
    uint32_t cast_count;
    uint32_t solid_count;
    uint32_t avg_r;
    uint32_t avg_g;
    uint32_t avg_b;
    uint32_t r_min;
    uint32_t r_max;
    uint32_t g_min;
    uint32_t g_max;
    uint32_t b_min;
    uint32_t b_max;
    uint16_t first;
    uint16_t center;
    bool bad;
} app_camera_frame_stats_t;
static bool app_camera_rgb565_close(uint16_t a, uint16_t b)
{
    const uint32_t ar = (uint32_t)((a >> 11) & 0x1FU);
    const uint32_t ag = (uint32_t)((a >> 5) & 0x3FU);
    const uint32_t ab = (uint32_t)(a & 0x1FU);
    const uint32_t br = (uint32_t)((b >> 11) & 0x1FU);
    const uint32_t bg = (uint32_t)((b >> 5) & 0x3FU);
    const uint32_t bb = (uint32_t)(b & 0x1FU);
    const uint32_t dr = (ar > br) ? (ar - br) : (br - ar);
    const uint32_t dg = (ag > bg) ? (ag - bg) : (bg - ag);
    const uint32_t db = (ab > bb) ? (ab - bb) : (bb - ab);
    return dr <= 1U && dg <= 2U && db <= 1U;
}
static bool app_camera_rgb565_cyan_like(uint16_t p)
{
    const uint32_t r = (uint32_t)((p >> 11) & 0x1FU);
    const uint32_t g = (uint32_t)((p >> 5) & 0x3FU);
    const uint32_t b = (uint32_t)(p & 0x1FU);
    return r <= 12U && g >= 45U && (b >= 12U || g >= 55U);
}
static uint16_t app_camera_swap_rgb565(uint16_t p)
{
    return (uint16_t)((p << 8) | (p >> 8));
}
static bool app_camera_analyze_rgb565_frame(const uint8_t *buf,
    size_t len,
    uint32_t width,
    uint32_t height,
    app_camera_frame_stats_t *stats_out)
{
    app_camera_frame_stats_t stats = {
        .r_min = 31U,
        .g_min = 63U,
        .b_min = 31U,
    };
    if (buf == NULL || width == 0U || height == 0U || len < ((size_t)width * height * 2U))
    {
        stats.bad = true;
        if (stats_out != NULL)
        {
            *stats_out = stats;
        }
        return true;
    }
    const uint16_t *pixels = (const uint16_t *)buf;
    uint32_t r_sum = 0;
    uint32_t g_sum = 0;
    uint32_t b_sum = 0;
    const uint32_t first_x = width / (PREVIEW_BAD_SAMPLE_COLS * 2U);
    const uint32_t first_y = height / (PREVIEW_BAD_SAMPLE_ROWS * 2U);
    stats.first = pixels[(size_t)first_y * width + first_x];
    stats.center = pixels[(size_t)(height / 2U) * width + (width / 2U)];
    for (uint32_t row = 0; row < PREVIEW_BAD_SAMPLE_ROWS; row++) {
        uint32_t y = ((row * height) + (height / (PREVIEW_BAD_SAMPLE_ROWS * 2U))) /
            PREVIEW_BAD_SAMPLE_ROWS;
        if (y >= height)
        {
            y = height - 1U;
        }
        for (uint32_t col = 0; col < PREVIEW_BAD_SAMPLE_COLS; col++) {
            uint32_t x = ((col * width) + (width / (PREVIEW_BAD_SAMPLE_COLS * 2U))) /
                PREVIEW_BAD_SAMPLE_COLS;
            if (x >= width)
            {
                x = width - 1U;
            }
            const uint16_t p = pixels[(size_t)y * width + x];
            const uint32_t r = (uint32_t)((p >> 11) & 0x1FU);
            const uint32_t g = (uint32_t)((p >> 5) & 0x3FU);
            const uint32_t b = (uint32_t)(p & 0x1FU);
            if (r < stats.r_min) stats.r_min = r;
            if (r > stats.r_max) stats.r_max = r;
            if (g < stats.g_min) stats.g_min = g;
            if (g > stats.g_max) stats.g_max = g;
            if (b < stats.b_min) stats.b_min = b;
            if (b > stats.b_max) stats.b_max = b;
            r_sum += r;
            g_sum += g;
            b_sum += b;
            if (app_camera_rgb565_cyan_like(p))
            {
                stats.cyan_count++;
            }
            if (app_camera_rgb565_cyan_like(app_camera_swap_rgb565(p)))
            {
                stats.swapped_cyan_count++;
            }
            if (r <= 14U && g >= 32U && b >= 10U && (g + b) >= ((r + 1U) * 4U))
            {
                stats.cast_count++;
            }
            if (app_camera_rgb565_close(stats.first, p))
            {
                stats.solid_count++;
            }
            stats.sample_count++;
        }
    }
    if (stats.sample_count == 0U)
    {
        stats.bad = true;
        if (stats_out != NULL)
        {
            *stats_out = stats;
        }
        return true;
    }
    stats.avg_r = r_sum / stats.sample_count;
    stats.avg_g = g_sum / stats.sample_count;
    stats.avg_b = b_sum / stats.sample_count;
    if ((stats.cyan_count * 100U) >= (stats.sample_count * PREVIEW_BAD_CYAN_PERCENT) ||
        (stats.swapped_cyan_count * 100U) >= (stats.sample_count * PREVIEW_BAD_CYAN_PERCENT))
    {
        stats.bad = true;
    }
    if ((stats.cast_count * 100U) >= (stats.sample_count * PREVIEW_BAD_CAST_PERCENT) &&
        stats.avg_r <= 14U && stats.avg_g >= 30U && stats.avg_b >= 10U)
    {
        stats.bad = true;
    }
    if ((stats.solid_count * 100U) >= (stats.sample_count * PREVIEW_BAD_SOLID_PERCENT))
    {
        const uint32_t avg = stats.avg_r + stats.avg_g + stats.avg_b;
        if (avg > 8U)
        {
            stats.bad = true;
        }
    }
    const bool low_variation =
        ((stats.r_max - stats.r_min) <= 1U) &&
        ((stats.g_max - stats.g_min) <= 2U) &&
        ((stats.b_max - stats.b_min) <= 1U);
    if (low_variation && (stats.avg_r + stats.avg_g + stats.avg_b) > 12U)
    {
        stats.bad = true;
    }
    if (stats_out != NULL)
    {
        *stats_out = stats;
    }
    return stats.bad;
}
static void app_camera_log_frame_stats(const char *stage,
    uint32_t seq,
    const app_camera_frame_stats_t *stats)
{
    if (stage == NULL || stats == NULL || s_bad_detail_log_count >= PREVIEW_BAD_DETAIL_LOG_LIMIT)
    {
        return;
    }
    s_bad_detail_log_count++;
    ESP_LOGW(TAG,
        "preview %s suspicious seq=%lu bad=%d sample=%lu cyan=%lu swap_cyan=%lu cast=%lu solid=%lu avg=(%lu,%lu,%lu) range=(%lu-%lu,%lu-%lu,%lu-%lu) first=0x%04x center=0x%04x",
        stage,
        (unsigned long)seq,
        (int)stats->bad,
        (unsigned long)stats->sample_count,
        (unsigned long)stats->cyan_count,
        (unsigned long)stats->swapped_cyan_count,
        (unsigned long)stats->cast_count,
        (unsigned long)stats->solid_count,
        (unsigned long)stats->avg_r,
        (unsigned long)stats->avg_g,
        (unsigned long)stats->avg_b,
        (unsigned long)stats->r_min,
        (unsigned long)stats->r_max,
        (unsigned long)stats->g_min,
        (unsigned long)stats->g_max,
        (unsigned long)stats->b_min,
        (unsigned long)stats->b_max,
        stats->first,
        stats->center);
}
static bool app_camera_should_hold_bad_preview(void)
{
    s_bad_preview_streak++;
    return (s_display_count > 0U) && (s_bad_preview_streak <= PREVIEW_BAD_HOLD_FRAMES);
}
static void app_camera_note_good_preview(void)
{
    s_bad_preview_streak = 0;
}
// CPU fallback 缩放路径：最近邻采样到屏幕中央区域。
static esp_err_t app_camera_preview_scale_rgb565_cpu(const uint8_t *in_buf,
    uint32_t in_width,
    uint32_t in_height,
    uint8_t *out_buf,
    uint32_t out_width,
    uint32_t out_height)
{
    if (in_buf == NULL || out_buf == NULL || in_width == 0 || in_height == 0 ||
        out_width == 0 || out_height == 0 ||
        out_width > BSP_LCD_H_RES || out_height > BSP_LCD_V_RES)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t *src = (const uint16_t *)in_buf;
    uint16_t *dst = (uint16_t *)out_buf;
    const uint32_t x_off = (BSP_LCD_H_RES - out_width) / 2U;
    const uint32_t y_off = (BSP_LCD_V_RES - out_height) / 2U;
    if (in_width == out_width && in_height == out_height &&
        x_off == 0U && y_off == 0U)
    {
        memcpy(out_buf, in_buf, (size_t)out_width * out_height * 2U);
        return ESP_OK;
    }
    uint32_t src_y = 0;
    uint32_t src_y_acc = 0;
    for (uint32_t y = 0; y < out_height; y++) {
        if (src_y >= in_height)
        {
            src_y = in_height - 1U;
        }
        const uint16_t *src_row = src + (size_t)src_y * in_width;
        uint16_t *dst_row = dst + (size_t)(y + y_off) * BSP_LCD_H_RES + x_off;
        uint32_t src_x = 0;
        uint32_t src_x_acc = 0;
        for (uint32_t x = 0; x < out_width; x++) {
            if (src_x >= in_width)
            {
                src_x = in_width - 1U;
            }
            dst_row[x] = src_row[src_x];
            src_x_acc += in_width;
            while (src_x_acc >= out_width)
            {
                src_x_acc -= out_width;
                if ((src_x + 1U) < in_width)
                {
                    src_x++;
                }
                else
                {
                    break;
                }
            }
        }
        src_y_acc += in_height;
        while (src_y_acc >= out_height)
        {
            src_y_acc -= out_height;
            if ((src_y + 1U) < in_height)
            {
                src_y++;
            }
            else
            {
                break;
            }
        }
    }
    return ESP_OK;
}
static void app_camera_free_camera_buffers(void)
{
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {
        if (s_cam_buf[i])
        {
            heap_caps_free(s_cam_buf[i]);
            s_cam_buf[i] = NULL;
        }
    }
    s_cam_buf_size = 0;
}
static void app_camera_free_display_buffers(void)
{
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        if (s_stage_buf[i])
        {
            heap_caps_free(s_stage_buf[i]);
            s_stage_buf[i] = NULL;
        }
        s_stage_state[i] = DISP_BUF_FREE;
        s_stage_letterbox_w[i] = 0;
        s_stage_letterbox_h[i] = 0;
    }
    for (int i = 0; i < UI_CANVAS_NUM_BUFS; i++) {
        if (s_ui_canvas_buf[i])
        {
            heap_caps_free(s_ui_canvas_buf[i]);
            s_ui_canvas_buf[i] = NULL;
        }
    }
    s_ui_canvas_active_index = 0;
    s_pending_stage_index = -1;
    s_displayed_stage_index = -1;
    s_retained_stage_index = -1;
    s_disp_buf_size = 0;
}
// 分配 LVGL canvas 与 stage queue 缓冲区，均按 cache line 对齐。
static esp_err_t app_camera_alloc_display_buffers(void)
{
    esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_disp_buf_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_line_size);
    for (int i = 0; i < UI_CANVAS_NUM_BUFS; i++) {
        s_ui_canvas_buf[i] = app_camera_aligned_calloc(s_disp_buf_size, DISPLAY_BUF_CAPS, "ui canvas");
        if (s_ui_canvas_buf[i] == NULL)
        {
            ESP_LOGE(TAG, "alloc ui canvas buffer %d failed", i);
            app_camera_free_display_buffers();
            return ESP_ERR_NO_MEM;
        }
        ret = app_camera_msync_c2m(s_ui_canvas_buf[i], s_disp_buf_size);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "sync ui canvas buffer %d failed: %s", i, esp_err_to_name(ret));
            app_camera_free_display_buffers();
            return ret;
        }
    }
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        s_stage_buf[i] = app_camera_aligned_calloc(s_disp_buf_size, DISPLAY_BUF_CAPS, "stage buffer");
        if (s_stage_buf[i] == NULL)
        {
            ESP_LOGE(TAG, "alloc stage buffer %d failed", i);
            app_camera_free_display_buffers();
            return ESP_ERR_NO_MEM;
        }
        ret = app_camera_msync_c2m(s_stage_buf[i], s_disp_buf_size);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "sync stage buffer %d failed: %s", i, esp_err_to_name(ret));
            app_camera_free_display_buffers();
            return ret;
        }
        s_stage_state[i] = DISP_BUF_FREE;
    }
    return ESP_OK;
}
// 分配 V4L2 USERPTR 帧缓存，供摄像头驱动直接写入。
static esp_err_t app_camera_alloc_userptr_buffers(size_t frame_size)
{
    if (s_cache_line_size == 0)
    {
        esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    s_cam_buf_size = ALIGN_UP(frame_size, s_cache_line_size);
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {
        s_cam_buf[i] = app_camera_aligned_calloc(s_cam_buf_size, CAMERA_BUF_CAPS, "camera buffer");
        if (s_cam_buf[i] == NULL)
        {
            ESP_LOGE(TAG, "alloc USERPTR camera buffer %d failed", i);
            app_camera_free_camera_buffers();
            return ESP_ERR_NO_MEM;
        }
        esp_err_t ret = app_camera_msync_c2m(s_cam_buf[i], s_cam_buf_size);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "sync USERPTR camera buffer %d failed: %s", i, esp_err_to_name(ret));
            app_camera_free_camera_buffers();
            return ret;
        }
    }
    ESP_LOGI(TAG, "USERPTR camera buffers ready: %d x %u bytes", CAMERA_NUM_BUFS, (unsigned)s_cam_buf_size);
    return ESP_OK;
}
// 创建固定尺寸 canvas；之后只切换 buffer 指针，不反复创建对象。
static esp_err_t app_camera_create_canvas(void)
{
    if (s_camera_canvas != NULL)
    {
        return ESP_OK;
    }
    if (!bsp_display_lock(DISPLAY_CREATE_LOCK_TIMEOUT_MS))
    {
        return ESP_FAIL;
    }
    lv_obj_t *scr = app_get_active_screen();
    s_camera_canvas = lv_canvas_create(scr);
    if (s_camera_canvas == NULL)
    {
        bsp_display_unlock();
        return ESP_FAIL;
    }
    app_camera_canvas_set_buffer(s_ui_canvas_buf[s_ui_canvas_active_index]);
    lv_obj_center(s_camera_canvas);
    lv_obj_move_background(s_camera_canvas);
    bsp_display_unlock();
    return ESP_OK;
}
#if SOC_PPA_SUPPORTED
// 注册 PPA SRM 客户端，用硬件完成缩放/旋转。
static void app_ppa_init(void)
{
    if (s_ppa_srm_handle)
    {
        return;
    }
    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    esp_err_t ret = ppa_register_client(&cfg, &s_ppa_srm_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ppa_register_client failed: 0x%x", ret);
    }
}
// 清理预览上下或左右黑边，避免上一帧残留。
static esp_err_t app_camera_clear_letterbox(uint8_t *out_buf,
    uint32_t out_width,
    uint32_t out_height)
{
    if (out_buf == NULL || out_width > BSP_LCD_H_RES || out_height > BSP_LCD_V_RES)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t pixel_size = 2U;
    const size_t row_stride = (size_t)BSP_LCD_H_RES * pixel_size;
    const uint32_t x_off = (BSP_LCD_H_RES - out_width) / 2U;
    const uint32_t y_off = (BSP_LCD_V_RES - out_height) / 2U;
    const uint32_t bottom_y = y_off + out_height;
    const uint32_t right_x = x_off + out_width;
    if (y_off > 0)
    {
        size_t bytes = (size_t)y_off * row_stride;
        memset(out_buf, 0, bytes);
        esp_err_t ret = app_camera_msync_c2m(out_buf, bytes);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    if (bottom_y < BSP_LCD_V_RES)
    {
        uint8_t *bottom = out_buf + (size_t)bottom_y * row_stride;
        size_t bytes = (size_t)(BSP_LCD_V_RES - bottom_y) * row_stride;
        memset(bottom, 0, bytes);
        esp_err_t ret = app_camera_msync_c2m(bottom, bytes);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    if (x_off > 0 || right_x < BSP_LCD_H_RES)
    {
        const size_t left_bytes = (size_t)x_off * pixel_size;
        const size_t right_bytes = (size_t)(BSP_LCD_H_RES - right_x) * pixel_size;
        for (uint32_t y = y_off; y < bottom_y; y++) {
            uint8_t *row = out_buf + (size_t)y * row_stride;
            if (left_bytes > 0)
            {
                memset(row, 0, left_bytes);
            }
            if (right_bytes > 0)
            {
                memset(row + (size_t)right_x * pixel_size, 0, right_bytes);
            }
        }
        uint8_t *rows = out_buf + (size_t)y_off * row_stride;
        size_t bytes = (size_t)out_height * row_stride;
        esp_err_t ret = app_camera_msync_c2m(rows, bytes);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    return ESP_OK;
}
// 只有预览尺寸变化时才重刷黑边，减少每帧 memset 开销。
static esp_err_t app_camera_prepare_stage_background(int stage_index,
    uint8_t *out_buf,
    uint32_t out_width,
    uint32_t out_height)
{
    if (stage_index < 0 || stage_index >= STAGE_NUM_BUFS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_stage_letterbox_w[stage_index] == out_width &&
        s_stage_letterbox_h[stage_index] == out_height)
    {
        return ESP_OK;
    }
    esp_err_t ret = app_camera_clear_letterbox(out_buf, out_width, out_height);
    if (ret == ESP_OK)
    {
        s_stage_letterbox_w[stage_index] = out_width;
        s_stage_letterbox_h[stage_index] = out_height;
    }
    return ret;
}
// PPA 缩放/旋转入口，输出固定到整屏 RGB565 stage buffer。
static esp_err_t app_image_process_scale_crop(uint8_t *in_buf,
    uint32_t in_width,
    uint32_t in_height,
    uint8_t *out_buf,
    uint32_t out_width,
    uint32_t out_height,
    size_t out_buf_size,
    ppa_srm_rotation_angle_t rotation_angle)
{
    float scale_x = (float)out_width / (float)in_width;
    float scale_y = (float)out_height / (float)in_height;
    if (rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || rotation_angle == PPA_SRM_ROTATION_ANGLE_270)
    {
        scale_x = (float)out_height / (float)in_width;
        scale_y = (float)out_width / (float)in_height;
    }
    ppa_srm_oper_config_t srm_cfg = {
        .in.buffer = in_buf,
        .in.pic_w = in_width,
        .in.pic_h = in_height,
        .in.block_w = in_width,
        .in.block_h = in_height,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = out_buf,
        .out.buffer_size = out_buf_size,
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = (BSP_LCD_H_RES - out_width) / 2,
        .out.block_offset_y = (BSP_LCD_V_RES - out_height) / 2,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = rotation_angle,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    return ppa_do_scale_rotate_mirror(s_ppa_srm_handle, &srm_cfg);
}
static ppa_srm_rotation_angle_t app_camera_ppa_rotation(void)
{
    switch (BSP_CAMERA_ROTATION) {
    case 90:  return PPA_SRM_ROTATION_ANGLE_90;
    case 180: return PPA_SRM_ROTATION_ANGLE_180;
    case 270: return PPA_SRM_ROTATION_ANGLE_270;
    case 0:
    default:  return PPA_SRM_ROTATION_ANGLE_0;
    }
}
static void app_camera_calc_preview_fit(uint32_t camera_width,
    uint32_t camera_height,
    uint32_t *out_w,
    uint32_t *out_h)
{
    if ((BSP_CAMERA_ROTATION == 90) || (BSP_CAMERA_ROTATION == 270))
    {
        app_camera_preview_calc_aspect_fit(camera_height, camera_width, BSP_LCD_H_RES, BSP_LCD_V_RES, out_w, out_h);
    }
    else
    {
        app_camera_preview_calc_aspect_fit(camera_width, camera_height, BSP_LCD_H_RES, BSP_LCD_V_RES, out_w, out_h);
    }
}
#endif
// stage queue 只保留最新待显示帧，显示跟不上时主动丢帧。
static bool app_camera_has_pending_stage(void)
{
    bool has_pending = false;
    taskENTER_CRITICAL(&s_display_mux);
    has_pending = (s_pending_stage_index >= 0);
    taskEXIT_CRITICAL(&s_display_mux);
    return has_pending;
}
// 申请一个空闲 stage buffer 供相机回调写入。
static int app_camera_pick_writable_stage_buffer(void)
{
    int idx = -1;
    taskENTER_CRITICAL(&s_display_mux);
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        if (s_stage_state[i] == DISP_BUF_FREE)
        {
            s_stage_state[i] = DISP_BUF_WRITING;
            idx = i;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_display_mux);
    return idx;
}
// 发布新帧；若旧 ready 帧还未显示，则丢弃旧帧保证低延迟。
static void app_camera_publish_stage_buffer(int ready_index)
{
    taskENTER_CRITICAL(&s_display_mux);
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY)
    {
        s_stage_state[s_pending_stage_index] = DISP_BUF_FREE;
        s_stage_drop_count++;
    }
    s_stage_state[ready_index] = DISP_BUF_READY;
    s_pending_stage_index = ready_index;
    taskEXIT_CRITICAL(&s_display_mux);
    if (s_display_task_handle)
    {
        xTaskNotifyGive(s_display_task_handle);
    }
}
static void app_camera_abandon_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS)
    {
        return;
    }
    taskENTER_CRITICAL(&s_display_mux);
    if (s_stage_state[buf_index] == DISP_BUF_WRITING)
    {
        s_stage_state[buf_index] = DISP_BUF_FREE;
    }
    taskEXIT_CRITICAL(&s_display_mux);
}
static int app_camera_take_ready_stage_buffer(void)
{
    int idx = -1;
    taskENTER_CRITICAL(&s_display_mux);
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY)
    {
        idx = s_pending_stage_index;
        s_pending_stage_index = -1;
    }
    taskEXIT_CRITICAL(&s_display_mux);
    return idx;
}
static void app_camera_release_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS)
    {
        return;
    }
    taskENTER_CRITICAL(&s_display_mux);
    s_stage_state[buf_index] = DISP_BUF_FREE;
    if (s_displayed_stage_index == buf_index)
    {
        s_displayed_stage_index = -1;
    }
    if (s_retained_stage_index == buf_index)
    {
        s_retained_stage_index = -1;
    }
    if (s_pending_stage_index == buf_index)
    {
        s_pending_stage_index = -1;
    }
    taskEXIT_CRITICAL(&s_display_mux);
}
// 显示任务把已验证 stage 帧复制到三缓冲 canvas，避免 LVGL 读图时被覆盖。
static void app_camera_display_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (1) {
            int buf_index = app_camera_take_ready_stage_buffer();
            if (buf_index < 0 || s_camera_canvas == NULL || !app_camera_canvas_buffers_ready())
            {
                break;
            }
            if (app_camera_msync_m2c(s_stage_buf[buf_index], s_disp_buf_size) != ESP_OK)
            {
                app_camera_release_stage_buffer(buf_index);
                continue;
            }
            bool displayed = false;
            if (bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS))
            {
                app_camera_frame_stats_t canvas_stats = {0};
                if (app_camera_analyze_rgb565_frame(s_stage_buf[buf_index],
                        s_disp_buf_size,
                        BSP_LCD_H_RES,
                        BSP_LCD_V_RES,
                        &canvas_stats))
                {
                    s_canvas_bad_count++;
                    app_camera_log_frame_stats("canvas", s_frame_count, &canvas_stats);
                }
                uint8_t next_index = (uint8_t)((s_ui_canvas_active_index + 1U) % UI_CANVAS_NUM_BUFS);
                memcpy(s_ui_canvas_buf[next_index], s_stage_buf[buf_index], s_disp_buf_size);
                if (app_camera_msync_c2m(s_ui_canvas_buf[next_index], s_disp_buf_size) == ESP_OK)
                {
                    app_camera_canvas_set_buffer(s_ui_canvas_buf[next_index]);
                    s_ui_canvas_active_index = next_index;
                    lv_obj_invalidate(s_camera_canvas);
                    displayed = true;
                }
                bsp_display_unlock();
            }
            if (displayed)
            {
                s_display_count++;
                if (s_display_count == 1U)
                {
                    ESP_LOGI(TAG, "first camera frame handed to LVGL canvas");
                }
                app_camera_release_stage_buffer(buf_index);
            }
            else
            {
                app_camera_release_stage_buffer(buf_index);
            }
        }
    }
}
// V4L2 每帧回调：抽样提交识别模块，并生成一帧可显示的 stage buffer。
static void app_camera_frame_cb(uint8_t *camera_buf,
    uint8_t camera_buf_index,
    uint32_t camera_buf_hes,
    uint32_t camera_buf_ves,
    size_t camera_buf_len)
{
    (void)camera_buf_index;
    if (camera_buf == NULL || s_camera_canvas == NULL || !app_camera_canvas_buffers_ready())
    {
        return;
    }
    if (s_camera_paused)
    {
        return;
    }
    s_frame_count++;
    if (s_frame_count == 1U)
    {
        ESP_LOGI(TAG,
            "first camera frame received: %lux%lu len=%lu",
            (unsigned long)camera_buf_hes,
            (unsigned long)camera_buf_ves,
            (unsigned long)camera_buf_len);
    }
    if (!app_camera_frame_length_ok(camera_buf_hes, camera_buf_ves, camera_buf_len))
    {
        s_bad_len_count++;
        app_camera_route_maybe_log_diag(s_frame_count,
            s_display_count,
            s_stage_drop_count,
            s_bad_len_count,
            s_bad_preview_count,
            s_ppa_guard_count,
            s_cpu_fallback_count,
            s_raw_bad_count,
            s_canvas_bad_count);
        return;
    }
    if (s_warmup_drop_remaining > 0U)
    {
        s_warmup_drop_remaining--;
        s_stage_drop_count++;
        app_camera_route_maybe_log_diag(s_frame_count,
            s_display_count,
            s_stage_drop_count,
            s_bad_len_count,
            s_bad_preview_count,
            s_ppa_guard_count,
            s_cpu_fallback_count,
            s_raw_bad_count,
            s_canvas_bad_count);
        return;
    }
    app_camera_route_maybe_log_diag(s_frame_count,
        s_display_count,
        s_stage_drop_count,
        s_bad_len_count,
        s_bad_preview_count,
        s_ppa_guard_count,
        s_cpu_fallback_count,
        s_raw_bad_count,
        s_canvas_bad_count);
    bool camera_synced_for_cpu = false;
    app_camera_frame_route_t route = app_camera_route_select();
    // 只有识别/抓图需要 CPU 读帧时才做 M2C cache 同步。
    if (route.ai_due || route.vision_due || route.capture_due)
    {
        if (app_camera_msync_m2c(camera_buf, camera_buf_len) == ESP_OK)
        {
            camera_synced_for_cpu = true;
            if (route.ai_due)
            {
                (void)app_drone_ai_submit_frame(camera_buf,
                    camera_buf_hes,
                    camera_buf_ves,
                    camera_buf_len);
                app_camera_route_note_ai_submit();
            }
            if (route.vision_due)
            {
                (void)app_vision_submit_frame(camera_buf,
                    camera_buf_hes,
                    camera_buf_ves,
                    camera_buf_len);
                app_camera_route_note_vision_submit();
            }
            if (route.capture_due)
            {
                (void)app_ai_capture_submit_frame(camera_buf,
                    camera_buf_hes,
                    camera_buf_ves,
                    camera_buf_len);
                app_camera_route_note_capture_submit();
            }
        }
    }
    // 显示端已有待处理帧时直接丢弃当前预览帧，保证实时性优先。
    if (app_camera_has_pending_stage())
    {
        s_stage_drop_count++;
        return;
    }
    int stage_index = app_camera_pick_writable_stage_buffer();
    if (stage_index < 0)
    {
        s_stage_drop_count++;
        return;
    }
    uint32_t out_w = BSP_LCD_H_RES;
    uint32_t out_h = BSP_LCD_V_RES;
    uint8_t *out_buf = s_stage_buf[stage_index];
    bool stage_written_by_cpu = false;
    bool stage_written_by_ppa = false;
#if SOC_PPA_SUPPORTED
    if (s_ppa_srm_handle)
    {
        esp_err_t ppa_ret;
        ppa_srm_rotation_angle_t rotation = app_camera_ppa_rotation();
        app_camera_calc_preview_fit(camera_buf_hes, camera_buf_ves, &out_w, &out_h);
        if (app_camera_prepare_stage_background(stage_index, out_buf, out_w, out_h) != ESP_OK)
        {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        // 首选 PPA 硬件缩放；失败时丢弃当前帧，屏幕保持上一帧好画面。
        ppa_ret = app_image_process_scale_crop(camera_buf,
            camera_buf_hes,
            camera_buf_ves,
            out_buf,
            out_w,
            out_h,
            s_disp_buf_size,
            rotation);
        if (ppa_ret != ESP_OK)
        {
            if (!s_ppa_error_logged)
            {
                s_ppa_error_logged = true;
                ESP_LOGW(TAG,
                    "PPA preview path failed once (%s), keeping previous frame",
                    esp_err_to_name(ppa_ret));
            }
            s_ppa_guard_count++;
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        else
        {
            stage_written_by_ppa = true;
        }
    }
    if (!stage_written_by_ppa)
#endif
    {
        app_camera_calc_preview_fit(camera_buf_hes, camera_buf_ves, &out_w, &out_h);
        if (app_camera_prepare_stage_background(stage_index, out_buf, out_w, out_h) != ESP_OK)
        {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        if (!camera_synced_for_cpu && app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK)
        {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        if (app_camera_preview_scale_rgb565_cpu(camera_buf,
                camera_buf_hes,
                camera_buf_ves,
                out_buf,
                out_w,
                out_h) != ESP_OK)
        {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        s_cpu_fallback_count++;
        stage_written_by_cpu = true;
    }
    if (stage_written_by_ppa)
    {
        if (app_camera_msync_m2c(out_buf, s_disp_buf_size) != ESP_OK)
        {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        app_camera_frame_stats_t ppa_stats = {0};
        if (app_camera_analyze_rgb565_frame(out_buf,
                s_disp_buf_size,
                BSP_LCD_H_RES,
                BSP_LCD_V_RES,
                &ppa_stats))
        {
            s_bad_preview_count++;
            app_camera_log_frame_stats("ppa", s_frame_count, &ppa_stats);
            app_camera_frame_stats_t raw_stats = {0};
            if ((!camera_synced_for_cpu && app_camera_msync_m2c(camera_buf, camera_buf_len) == ESP_OK) ||
                camera_synced_for_cpu)
            {
                if (app_camera_analyze_rgb565_frame(camera_buf,
                        camera_buf_len,
                        camera_buf_hes,
                        camera_buf_ves,
                        &raw_stats))
                {
                    s_raw_bad_count++;
                }
                app_camera_log_frame_stats("raw_at_ppa_bad", s_frame_count, &raw_stats);
            }
            if (app_camera_should_hold_bad_preview())
            {
                app_camera_abandon_stage_buffer(stage_index);
                return;
            }
        }
        else
        {
            app_camera_note_good_preview();
        }
    }
    if (stage_written_by_cpu)
    {
        app_camera_frame_stats_t cpu_stats = {0};
        if (app_camera_analyze_rgb565_frame(out_buf,
                s_disp_buf_size,
                BSP_LCD_H_RES,
                BSP_LCD_V_RES,
                &cpu_stats))
        {
            s_bad_preview_count++;
            app_camera_log_frame_stats("cpu", s_frame_count, &cpu_stats);
            if (app_camera_should_hold_bad_preview())
            {
                app_camera_abandon_stage_buffer(stage_index);
                return;
            }
        }
        else
        {
            app_camera_note_good_preview();
        }
    }
    if (stage_written_by_cpu && app_camera_msync_c2m(out_buf, s_disp_buf_size) != ESP_OK)
    {
        app_camera_abandon_stage_buffer(stage_index);
        return;
    }
    app_camera_publish_stage_buffer(stage_index);
}
// 启动显示任务，PSRAM 栈失败时自动退回内部 RAM 栈。
static esp_err_t app_camera_start_display_task(void)
{
    if (s_display_task_handle)
    {
        return ESP_OK;
    }
#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(app_camera_display_task,
        "cam_display",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &s_display_task_handle,
        1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        ESP_LOGW(TAG, "create display task with PSRAM stack failed, try internal stack");
        ret = xTaskCreatePinnedToCore(app_camera_display_task,
            "cam_display",
            DISPLAY_TASK_STACK_SIZE,
            NULL,
            DISPLAY_TASK_PRIORITY,
            &s_display_task_handle,
            1);
    }
#else
    BaseType_t ret = xTaskCreatePinnedToCore(app_camera_display_task,
        "cam_display",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &s_display_task_handle,
        1);
#endif
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
// 初始化摄像头预览链路的静态资源和回调。
esp_err_t app_camera_init(void)
{
    if (s_camera_inited)
    {
        return ESP_OK;
    }
#if !BSP_CAPS_CAMERA
    ESP_LOGE(TAG, "this BSP does not support camera");
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_LOGD(TAG, "starting camera preview pipeline (USERPTR + stage queue + fixed canvas)");
    esp_err_t ret = bsp_camera_start(NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "bsp_camera_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
#if SOC_PPA_SUPPORTED
    app_ppa_init();
#endif
    ret = app_camera_alloc_display_buffers();
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = app_camera_create_canvas();
    if (ret != ESP_OK)
    {
        app_camera_free_display_buffers();
        return ret;
    }
    ret = app_camera_start_display_task();
    if (ret != ESP_OK)
    {
        app_camera_free_display_buffers();
        return ret;
    }
    ret = app_video_register_frame_operation_cb(app_camera_frame_cb);
    if (ret != ESP_OK)
    {
        app_camera_free_display_buffers();
        return ret;
    }
    s_camera_inited = true;
    return ESP_OK;
#endif
}
// 打开视频设备、设置识别曝光参数并启动流任务。
esp_err_t app_camera_preview_start(void)
{
    if (!s_camera_inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_preview_running)
    {
        return ESP_OK;
    }
    s_video_fd = app_video_open_preferred(BSP_CAMERA_DEVICE,
        APP_VIDEO_FMT_RGB565,
        PREVIEW_PREFERRED_WIDTH,
        PREVIEW_PREFERRED_HEIGHT);
    if (s_video_fd < 0)
    {
        ESP_LOGE(TAG, "app_video_open failed");
        ESP_LOGW(TAG, "please check camera sensor selection in menuconfig");
        return ESP_FAIL;
    }
#if RECOGNITION_CAMERA_PROFILE_ENABLE
    esp_err_t profile_ret = app_video_apply_recognition_profile(s_video_fd,
        RECOGNITION_CAMERA_EXPOSURE_US,
        RECOGNITION_CAMERA_GAIN_PERCENT);
    if (profile_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "recognition camera profile init skipped: %s", esp_err_to_name(profile_ret));
    }
#else
    ESP_LOGI(TAG, "recognition camera profile skipped, using sensor default exposure/gain");
#endif
    esp_err_t ret = app_camera_alloc_userptr_buffers(app_video_get_buf_size());
    if (ret != ESP_OK)
    {
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    ret = app_video_set_bufs(s_video_fd, CAMERA_NUM_BUFS, (const void **)s_cam_buf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "app_video_set_bufs(USERPTR) failed: %s", esp_err_to_name(ret));
        app_camera_free_camera_buffers();
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    s_display_count = 0;
    s_frame_count = 0;
    s_stage_drop_count = 0;
    s_bad_len_count = 0;
    s_bad_preview_count = 0;
    s_ppa_guard_count = 0;
    s_cpu_fallback_count = 0;
    s_raw_bad_count = 0;
    s_canvas_bad_count = 0;
    s_bad_detail_log_count = 0;
    s_warmup_drop_remaining = PREVIEW_WARMUP_DROP_FRAMES;
    s_bad_preview_streak = 0;
    app_camera_route_reset();
    ret = app_video_stream_task_start(s_video_fd, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "app_video_stream_task_start failed: %s", esp_err_to_name(ret));
        app_camera_free_camera_buffers();
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    s_preview_running = true;
    ESP_LOGI(TAG, "camera preview started");
    return ESP_OK;
}
uint32_t app_camera_display_count(void)
{
    return s_display_count;
}
// 等待显示计数推进，调用方可用它避免切屏时露出空白底层。
bool app_camera_wait_display_count_after(uint32_t previous, uint32_t timeout_ms)
{
    const uint32_t start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (s_display_count <= previous) {
        if (!s_preview_running)
        {
            return false;
        }
        if (timeout_ms != UINT32_MAX &&
            ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms) >= timeout_ms)
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}
// 等待首帧真正交给 LVGL，用于启动页结束条件。
bool app_camera_wait_first_frame(uint32_t timeout_ms)
{
    return app_camera_wait_display_count_after(0U, timeout_ms);
}
// 暂停只阻止帧处理，不销毁摄像头资源，便于任务恢复时快速继续。
void app_camera_pause(void)
{
    s_camera_paused = true;
}
void app_camera_resume(void)
{
    s_camera_paused = false;
    s_warmup_drop_remaining = PREVIEW_WARMUP_DROP_FRAMES;
}
