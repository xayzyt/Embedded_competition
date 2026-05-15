/* 实现说明：采集、显示和视觉识别解耦，避免预览画面被耗时识别卡住。 */
/*
 * app_camera.c - 摄像头采集、本地预览和视觉取样桥接模块
 *
 * 这个文件是 ESP32-P4 端摄像头链路的核心胶水层：
 * - 通过 app_video.c 打开 V4L2 摄像头设备并启动视频流；
 * - 分配 PSRAM 中的摄像头 USERPTR 缓冲区，降低内部 SRAM 压力；
 * - 使用 PPA（如果芯片/SDK支持）把摄像头画面缩放/旋转到屏幕画布；
 * - 用三缓冲 stage buffer 避免摄像头采集任务和 LVGL 刷新任务互相阻塞；
 * - 按一定间隔把画面送给 app_vision.c 做 AprilTag 识别。
 *
 * 这类代码最重要的是“非阻塞”和“缓存一致性”：摄像头 DMA、PPA、CPU、LVGL 都可能访问同一片 PSRAM，
 * 所以文件里会看到 esp_cache_msync()、临界区、stage buffer 状态机等写法。
 */

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
#include "app_task.h"
#include "app_vision.h"
#if SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif

/* -------------------------------------------------------------------------- */
/* 缓冲、任务和识别配置                                   */
/* -------------------------------------------------------------------------- */

#define CAMERA_NUM_BUFS          4
#define STAGE_NUM_BUFS           5
#define ALIGN_UP(num, align)     (((num) + ((align) - 1)) & ~((align) - 1))
#define DISPLAY_TASK_STACK_SIZE  (6 * 1024)
#define DISPLAY_TASK_PRIORITY    7
#define DISPLAY_LOCK_TIMEOUT_MS  30
#define DISPLAY_CREATE_LOCK_TIMEOUT_MS 200
#define VISION_SAMPLE_INTERVAL   2
#define DRONE_AI_SAMPLE_INTERVAL 8
#define CAMERA_DIAG_INTERVAL_MS  2000U
#define PREVIEW_PREFERRED_WIDTH  1024U
#define PREVIEW_PREFERRED_HEIGHT 600U
#define RECOGNITION_CAMERA_EXPOSURE_US      4000U
#define RECOGNITION_CAMERA_GAIN_PERCENT     35U
#define CAMERA_BUF_CAPS          (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED)
#define DISPLAY_BUF_CAPS         (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA)
static const char *TAG = "app_camera";

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

static bool s_camera_inited = false;
static bool s_preview_running = false;
static bool s_camera_paused = false;
static int s_video_fd = -1;
static lv_obj_t *s_camera_canvas = NULL;
static size_t s_cache_line_size = 0;
static uint8_t *s_cam_buf[CAMERA_NUM_BUFS] = {0};
static size_t s_cam_buf_size = 0;
static uint8_t *s_stage_buf[STAGE_NUM_BUFS] = {0};
static uint8_t *s_ui_canvas_buf = NULL;
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
static uint32_t s_vision_sample_skip = 0;
static uint32_t s_drone_ai_sample_skip = 0;
static bool s_apriltag_gate_was_open = false;
static uint32_t s_frame_count = 0;
static volatile uint32_t s_display_count = 0;
static bool s_ppa_error_logged = false;
static disp_buf_state_t s_stage_state[STAGE_NUM_BUFS] = {0};
static uint32_t s_stage_letterbox_w[STAGE_NUM_BUFS] = {0};
static uint32_t s_stage_letterbox_h[STAGE_NUM_BUFS] = {0};
static volatile int s_retained_stage_index = -1;
static uint32_t s_stage_drop_count = 0;
static uint32_t s_ai_submit_count = 0;
static uint32_t s_vision_submit_count = 0;
static uint32_t s_capture_submit_count = 0;
static uint32_t s_diag_last_ms = 0;
static uint32_t s_diag_last_frame_count = 0;
static uint32_t s_diag_last_display_count = 0;
static uint32_t s_diag_last_stage_drop_count = 0;
static uint32_t s_diag_last_ai_submit_count = 0;
static uint32_t s_diag_last_vision_submit_count = 0;
static uint32_t s_diag_last_capture_submit_count = 0;
static uint32_t s_diag_last_ai_infer_count = 0;
static uint32_t s_diag_last_ai_drop_count = 0;
static uint32_t s_diag_last_vision_drop_count = 0;
#if SOC_PPA_SUPPORTED
static ppa_client_handle_t s_ppa_srm_handle = NULL;
#endif

/* -------------------------------------------------------------------------- */
/* 缓存和 LVGL 辅助函数                                                      */
/* -------------------------------------------------------------------------- */

/* 按 cache line 对齐后执行 PSRAM 缓存同步。 */
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
/* 将内存内容同步到 CPU 可安全读取的状态。 */
static inline esp_err_t app_camera_msync_m2c(const void *addr, size_t size)
{
    return app_camera_msync_aligned((void *)addr, size,
        ESP_CACHE_MSYNC_FLAG_DIR_M2C |
        ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}
/* 将 CPU 写入结果同步回外设/LVGL 可访问的状态。 */
static inline esp_err_t app_camera_msync_c2m(void *addr, size_t size)
{
    return app_camera_msync_aligned(addr, size,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M |
        ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

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

/* 兼容 LVGL 8/9 获取当前活动屏幕对象。 */
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}

/* -------------------------------------------------------------------------- */
/* 缓冲区生命周期                                                            */
/* -------------------------------------------------------------------------- */

/* 释放摄像头 USERPTR 帧缓冲。 */
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
/* 释放预览 stage buffer 和 LVGL 画布缓冲。 */
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
    if (s_ui_canvas_buf)
    {

        heap_caps_free(s_ui_canvas_buf);
        s_ui_canvas_buf = NULL;
    }
    s_pending_stage_index = -1;
    s_displayed_stage_index = -1;
    s_retained_stage_index = -1;
    s_disp_buf_size = 0;
}
/* 在 PSRAM 中分配显示画布和三路 stage buffer。 */
static esp_err_t app_camera_alloc_display_buffers(void)
{
    esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_disp_buf_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_line_size);

    s_ui_canvas_buf = app_camera_aligned_calloc(s_disp_buf_size, DISPLAY_BUF_CAPS, "ui canvas");
    if (s_ui_canvas_buf == NULL)
    {
        ESP_LOGE(TAG, "alloc ui canvas buffer failed");

        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {

        s_stage_buf[i] = app_camera_aligned_calloc(s_disp_buf_size, DISPLAY_BUF_CAPS, "stage buffer");
        if (s_stage_buf[i] == NULL)
        {
            ESP_LOGE(TAG, "alloc stage buffer %d failed", i);
            app_camera_free_display_buffers();

            return ESP_ERR_NO_MEM;
        }
        s_stage_state[i] = DISP_BUF_FREE;
    }
    return ESP_OK;
}
/* 根据摄像头帧大小分配 V4L2 USERPTR 缓冲。 */
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
    }
    ESP_LOGI(TAG, "USERPTR camera buffers ready: %d x %u bytes", CAMERA_NUM_BUFS, (unsigned)s_cam_buf_size);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* 画布和 PPA 初始化                                                        */
/* -------------------------------------------------------------------------- */

/* 创建 LVGL canvas，并绑定预览使用的 RGB565 缓冲。 */
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
#if LVGL_VERSION_MAJOR >= 9

    lv_canvas_set_buffer(s_camera_canvas, s_ui_canvas_buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_COLOR_FORMAT_RGB565);
#else

    lv_canvas_set_buffer(s_camera_canvas, s_ui_canvas_buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
#endif
    lv_obj_center(s_camera_canvas);
    lv_obj_move_background(s_camera_canvas);

    bsp_display_unlock();
    return ESP_OK;
}
#if SOC_PPA_SUPPORTED
/* 注册 PPA 缩放旋转客户端，用于硬件加速预览缩放。 */
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
/* 按源宽高比计算适配目标区域后的显示尺寸。 */
static void calc_aspect_fit(uint32_t src_w, uint32_t src_h,
    uint32_t dst_w, uint32_t dst_h,
    uint32_t *out_w, uint32_t *out_h)
{
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
/* 清理预览画面的黑边区域，避免上一帧残影。 */
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
/* 调用 PPA 将摄像头帧缩放、旋转并写入显示 stage buffer。 */
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

static esp_err_t app_image_process_scale_cpu(const uint8_t *in_buf,
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

    for (uint32_t y = 0; y < out_height; y++) {
        uint32_t src_y = (uint32_t)(((uint64_t)y * in_height) / out_height);
        if (src_y >= in_height)
        {
            src_y = in_height - 1U;
        }
        const uint16_t *src_row = src + (size_t)src_y * in_width;
        uint16_t *dst_row = dst + (size_t)(y + y_off) * BSP_LCD_H_RES + x_off;

        for (uint32_t x = 0; x < out_width; x++) {
            uint32_t src_x = (uint32_t)(((uint64_t)x * in_width) / out_width);
            if (src_x >= in_width)
            {
                src_x = in_width - 1U;
            }
            dst_row[x] = src_row[src_x];
        }
    }

    return ESP_OK;
}

/* 把 BSP 摄像头旋转角度转换为 PPA 使用的枚举。 */
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

/* 结合旋转方向计算预览画面在屏幕中的适配尺寸。 */
static void app_camera_calc_preview_fit(uint32_t camera_width,
    uint32_t camera_height,
    uint32_t *out_w,
    uint32_t *out_h)
{
    if ((BSP_CAMERA_ROTATION == 90) || (BSP_CAMERA_ROTATION == 270))
    {
        calc_aspect_fit(camera_height, camera_width, BSP_LCD_H_RES, BSP_LCD_V_RES, out_w, out_h);
    }
    else
    {
        calc_aspect_fit(camera_width, camera_height, BSP_LCD_H_RES, BSP_LCD_V_RES, out_w, out_h);
    }
}
#endif

/* -------------------------------------------------------------------------- */
/* Stage 缓冲交接                                                        */
/* -------------------------------------------------------------------------- */

/* 判断是否已有等待显示任务消费的 stage buffer。 */
static bool app_camera_has_pending_stage(void)
{
    bool has_pending = false;

    taskENTER_CRITICAL(&s_display_mux);
    has_pending = (s_pending_stage_index >= 0);

    taskEXIT_CRITICAL(&s_display_mux);
    return has_pending;
}
/* 从 stage buffer 池中领取一个空闲缓冲作为写入目标。 */
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
/* 发布一帧已写好的 stage buffer，并唤醒显示任务。 */
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
/* 放弃当前写入中的 stage buffer，并重新标记为空闲。 */
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
/* 显示任务领取最新 ready 的 stage buffer。 */
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
/* 释放不再需要的 stage buffer，并清理相关索引。 */
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
/* 标记某个 stage buffer 已显示，并回收上一帧显示缓冲。 */
/* -------------------------------------------------------------------------- */
/* 显示任务和摄像头帧回调                                      */
/* -------------------------------------------------------------------------- */

/* 显示任务：把 ready stage buffer 绑定到 LVGL canvas 并刷新。 */
static void app_camera_mark_displayed_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS)
    {
        return;
    }

    taskENTER_CRITICAL(&s_display_mux);
    int old_retained = s_retained_stage_index;
    int old_displayed = s_displayed_stage_index;
    if (old_retained >= 0 && old_retained < STAGE_NUM_BUFS &&
        old_retained != old_displayed && old_retained != buf_index &&
        s_stage_state[old_retained] == DISP_BUF_DISPLAYED)
    {
        s_stage_state[old_retained] = DISP_BUF_FREE;
    }
    if (old_displayed >= 0 && old_displayed < STAGE_NUM_BUFS &&
        old_displayed != buf_index)
    {
        s_retained_stage_index = old_displayed;
    }
    else if (old_displayed < 0)
    {
        s_retained_stage_index = -1;
    }
    s_stage_state[buf_index] = DISP_BUF_DISPLAYED;
    s_displayed_stage_index = buf_index;
    taskEXIT_CRITICAL(&s_display_mux);
}

static void app_camera_display_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (1) {
            int buf_index = app_camera_take_ready_stage_buffer();
            if (buf_index < 0 || s_camera_canvas == NULL || s_ui_canvas_buf == NULL)
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
#if LVGL_VERSION_MAJOR >= 9
                lv_canvas_set_buffer(s_camera_canvas,
                    s_stage_buf[buf_index],
                    BSP_LCD_H_RES,
                    BSP_LCD_V_RES,
                    LV_COLOR_FORMAT_RGB565);
#else
                lv_canvas_set_buffer(s_camera_canvas,
                    s_stage_buf[buf_index],
                    BSP_LCD_H_RES,
                    BSP_LCD_V_RES,
                    LV_IMG_CF_TRUE_COLOR);
#endif
                lv_obj_invalidate(s_camera_canvas);
                displayed = true;

                bsp_display_unlock();
            }
            if (displayed)
            {
                s_display_count++;
                if (s_display_count == 1U)
                {
                    ESP_LOGI(TAG, "first camera frame handed to LVGL canvas");
                }
                app_camera_mark_displayed_stage_buffer(buf_index);
            }
            else
            {
                app_camera_release_stage_buffer(buf_index);
            }
        }
    }
}
/* 摄像头帧回调：抽样送视觉识别，同时生成预览 stage buffer。 */
static bool app_camera_apriltag_gate_open(void)
{
    app_task_snapshot_t task = {0};

    if (!app_task_peek_snapshot(&task))
    {
        return false;
    }
    return task.active &&
           (task.state == APP_TASK_STATE_WAIT_APPROACH ||
            task.state == APP_TASK_STATE_AUTH_PASSED) &&
           app_drone_ai_is_drone_confirmed();
}

static bool app_camera_ai_gate_active(void)
{
    app_task_snapshot_t task = {0};

    if (!app_task_peek_snapshot(&task))
    {
        return false;
    }
    return task.active &&
           (task.state == APP_TASK_STATE_WAIT_APPROACH) &&
           !app_drone_ai_is_drone_confirmed();
}

typedef struct {
    bool ai_due;
    bool vision_due;
    bool capture_due;
} app_camera_frame_route_t;

static app_camera_frame_route_t app_camera_select_frame_route(void)
{
    app_camera_frame_route_t route = {0};
    const bool capture_active = app_ai_capture_is_active();
    const bool ai_gate_active = app_camera_ai_gate_active();
    const bool apriltag_gate_open = app_camera_apriltag_gate_open();

    if (!ai_gate_active)
    {
        s_drone_ai_sample_skip = 0;
    }
    else if (++s_drone_ai_sample_skip >= DRONE_AI_SAMPLE_INTERVAL)
    {
        s_drone_ai_sample_skip = 0;
        route.ai_due = true;
    }
    if (capture_active || !apriltag_gate_open)
    {
        s_vision_sample_skip = 0;
        s_apriltag_gate_was_open = false;
    }
    else if (!s_apriltag_gate_was_open)
    {
        s_vision_sample_skip = 0;
        s_apriltag_gate_was_open = true;
        route.vision_due = true;
    }
    else if (++s_vision_sample_skip >= VISION_SAMPLE_INTERVAL)
    {
        s_vision_sample_skip = 0;
        route.vision_due = true;
    }
    route.capture_due = app_ai_capture_should_capture_frame();
    return route;
}

static void app_camera_maybe_log_diag(void)
{
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    app_drone_ai_stats_t ai = {0};
    app_vision_stats_t vision = {0};

    app_drone_ai_get_stats(&ai);
    app_vision_get_stats(&vision);
    const uint32_t vision_drop = vision.busy_drop + vision.overwrite;

    if (s_diag_last_ms == 0U)
    {
        s_diag_last_ms = now_ms;
        s_diag_last_frame_count = s_frame_count;
        s_diag_last_display_count = s_display_count;
        s_diag_last_stage_drop_count = s_stage_drop_count;
        s_diag_last_ai_submit_count = s_ai_submit_count;
        s_diag_last_vision_submit_count = s_vision_submit_count;
        s_diag_last_capture_submit_count = s_capture_submit_count;
        s_diag_last_ai_infer_count = ai.inferred;
        s_diag_last_ai_drop_count = ai.dropped;
        s_diag_last_vision_drop_count = vision_drop;
        return;
    }
    if ((now_ms - s_diag_last_ms) < CAMERA_DIAG_INTERVAL_MS)
    {
        return;
    }

    ESP_LOGI(TAG,
        "diag 2s: capture=%lu display=%lu stage_drop=%lu ai_submit=%lu ai_infer=%lu ai_drop=%lu vision_submit=%lu vision_drop=%lu save_submit=%lu confirmed=%d",
        (unsigned long)(s_frame_count - s_diag_last_frame_count),
        (unsigned long)(s_display_count - s_diag_last_display_count),
        (unsigned long)(s_stage_drop_count - s_diag_last_stage_drop_count),
        (unsigned long)(s_ai_submit_count - s_diag_last_ai_submit_count),
        (unsigned long)(ai.inferred - s_diag_last_ai_infer_count),
        (unsigned long)(ai.dropped - s_diag_last_ai_drop_count),
        (unsigned long)(s_vision_submit_count - s_diag_last_vision_submit_count),
        (unsigned long)(vision_drop - s_diag_last_vision_drop_count),
        (unsigned long)(s_capture_submit_count - s_diag_last_capture_submit_count),
        (int)ai.confirmed);

    s_diag_last_ms = now_ms;
    s_diag_last_frame_count = s_frame_count;
    s_diag_last_display_count = s_display_count;
    s_diag_last_stage_drop_count = s_stage_drop_count;
    s_diag_last_ai_submit_count = s_ai_submit_count;
    s_diag_last_vision_submit_count = s_vision_submit_count;
    s_diag_last_capture_submit_count = s_capture_submit_count;
    s_diag_last_ai_infer_count = ai.inferred;
    s_diag_last_ai_drop_count = ai.dropped;
    s_diag_last_vision_drop_count = vision_drop;
}

static void app_camera_frame_cb(uint8_t *camera_buf,
    uint8_t camera_buf_index,
    uint32_t camera_buf_hes,
    uint32_t camera_buf_ves,
    size_t camera_buf_len)
{
    (void)camera_buf_index;

    if (camera_buf == NULL || s_camera_canvas == NULL || s_ui_canvas_buf == NULL)
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
    app_camera_maybe_log_diag();

    bool camera_synced_for_cpu = false;
    app_camera_frame_route_t route = app_camera_select_frame_route();
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
                s_ai_submit_count++;
            }
            if (route.vision_due)
            {
                (void)app_vision_submit_frame(camera_buf,
                    camera_buf_hes,
                    camera_buf_ves,
                    camera_buf_len);
                s_vision_submit_count++;
            }
            if (route.capture_due)
            {
                (void)app_ai_capture_submit_frame(camera_buf,
                    camera_buf_hes,
                    camera_buf_ves,
                    camera_buf_len);
                s_capture_submit_count++;
            }
        }
    }
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
                    "PPA preview path failed once (%s), falling back to CPU scale",
                    esp_err_to_name(ppa_ret));
            }
            if (!camera_synced_for_cpu && app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK)
            {
                app_camera_abandon_stage_buffer(stage_index);
                return;
            }
            if (app_image_process_scale_cpu(camera_buf,
                    camera_buf_hes,
                    camera_buf_ves,
                    out_buf,
                    out_w,
                    out_h) != ESP_OK)
            {
                app_camera_abandon_stage_buffer(stage_index);
                return;
            }
            stage_written_by_cpu = true;
        }
    } else
#endif
    {
        if (!camera_synced_for_cpu && app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK)
        {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
        size_t copy_len = camera_buf_len < s_disp_buf_size ? camera_buf_len : s_disp_buf_size;
        memcpy(out_buf, camera_buf, copy_len);
        stage_written_by_cpu = true;
    }
    if (stage_written_by_cpu && app_camera_msync_c2m(out_buf, s_disp_buf_size) != ESP_OK)
    {
        app_camera_abandon_stage_buffer(stage_index);
        return;
    }
    app_camera_publish_stage_buffer(stage_index);
}
/* 创建预览显示任务，重复调用会直接成功返回。 */
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

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

/* 初始化摄像头预览流水线，完成缓冲、画布、任务和回调注册。 */
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
/* 打开 V4L2 视频流并启动摄像头帧采集。 */
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
    esp_err_t profile_ret = app_video_apply_recognition_profile(s_video_fd,
        RECOGNITION_CAMERA_EXPOSURE_US,
        RECOGNITION_CAMERA_GAIN_PERCENT);
    if (profile_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "recognition camera profile init skipped: %s", esp_err_to_name(profile_ret));
    }

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
    s_ai_submit_count = 0;
    s_vision_submit_count = 0;
    s_capture_submit_count = 0;
    s_vision_sample_skip = 0;
    s_drone_ai_sample_skip = 0;
    s_apriltag_gate_was_open = false;
    s_diag_last_ms = 0;
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

bool app_camera_wait_first_frame(uint32_t timeout_ms)
{
    const uint32_t start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    while (s_display_count == 0U) {
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

void app_camera_pause(void)
{
    s_camera_paused = true;
}

void app_camera_resume(void)
{
    s_camera_paused = false;
}
