#include "app_camera.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
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
#include "app_vision.h"
#if SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif
#define CAMERA_NUM_BUFS          4
#define STAGE_NUM_BUFS           3
#define ALIGN_UP(num, align)     (((num) + ((align) - 1)) & ~((align) - 1))
#define DISPLAY_TASK_STACK_SIZE  (6 * 1024)
#define DISPLAY_TASK_PRIORITY    7
#define VISION_SAMPLE_INTERVAL   6
static const char *TAG = "app_camera";
static bool s_camera_inited = false;
static bool s_preview_running = false;
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
} disp_buf_state_t;
static volatile int s_pending_stage_index = -1;
static uint32_t s_vision_sample_skip = 0;
static disp_buf_state_t s_stage_state[STAGE_NUM_BUFS] = {0};
#if SOC_PPA_SUPPORTED
static ppa_client_handle_t s_ppa_srm_handle = NULL;
#endif
static esp_err_t app_camera_msync_aligned(void *addr, size_t size, int flags)
{
    if (addr == NULL || size == 0) {
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
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
static void app_camera_free_camera_buffers(void)
{
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {
        if (s_cam_buf[i]) {
            heap_caps_free(s_cam_buf[i]);
            s_cam_buf[i] = NULL;
        }
    }
    s_cam_buf_size = 0;
}
static void app_camera_free_display_buffers(void)
{
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        if (s_stage_buf[i]) {
            heap_caps_free(s_stage_buf[i]);
            s_stage_buf[i] = NULL;
        }
        s_stage_state[i] = DISP_BUF_FREE;
    }
    if (s_ui_canvas_buf) {
        heap_caps_free(s_ui_canvas_buf);
        s_ui_canvas_buf = NULL;
    }
    s_pending_stage_index = -1;
    s_disp_buf_size = 0;
}
static esp_err_t app_camera_alloc_display_buffers(void)
{
    esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_disp_buf_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_line_size);
    s_ui_canvas_buf = heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM);
    if (s_ui_canvas_buf == NULL) {
        ESP_LOGE(TAG, "alloc ui canvas buffer failed");
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        s_stage_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM);
        if (s_stage_buf[i] == NULL) {
            ESP_LOGE(TAG, "alloc stage buffer %d failed", i);
            app_camera_free_display_buffers();
            return ESP_ERR_NO_MEM;
        }
        s_stage_state[i] = DISP_BUF_FREE;
    }
    return ESP_OK;
}
static esp_err_t app_camera_alloc_userptr_buffers(size_t frame_size)
{
    if (s_cache_line_size == 0) {
        esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    s_cam_buf_size = ALIGN_UP(frame_size, s_cache_line_size);
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {
        s_cam_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, s_cam_buf_size, MALLOC_CAP_SPIRAM);
        if (s_cam_buf[i] == NULL) {
            ESP_LOGE(TAG, "alloc USERPTR camera buffer %d failed", i);
            app_camera_free_camera_buffers();
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "USERPTR camera buffers ready: %d x %u bytes", CAMERA_NUM_BUFS, (unsigned)s_cam_buf_size);
    return ESP_OK;
}
static esp_err_t app_camera_create_canvas(void)
{
    if (s_camera_canvas != NULL) {
        return ESP_OK;
    }
    if (!bsp_display_lock(0)) {
        return ESP_FAIL;
    }
    lv_obj_t *scr = app_get_active_screen();
    s_camera_canvas = lv_canvas_create(scr);
    if (s_camera_canvas == NULL) {
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
static void app_ppa_init(void)
{
    if (s_ppa_srm_handle) {
        return;
    }
    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    esp_err_t ret = ppa_register_client(&cfg, &s_ppa_srm_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ppa_register_client failed: 0x%x", ret);
    }
}
static void calc_aspect_fit(uint32_t src_w, uint32_t src_h,
                            uint32_t dst_w, uint32_t dst_h,
                            uint32_t *out_w, uint32_t *out_h)
{
    const float src_aspect = (float)src_w / (float)src_h;
    const float dst_aspect = (float)dst_w / (float)dst_h;
    if (src_aspect > dst_aspect) {
        *out_w = dst_w;
        *out_h = (uint32_t)(dst_w / src_aspect);
    } else {
        *out_h = dst_h;
        *out_w = (uint32_t)(dst_h * src_aspect);
    }
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
    if (rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || rotation_angle == PPA_SRM_ROTATION_ANGLE_270) {
        scale_x = (float)out_height / (float)in_width;
        scale_y = (float)out_width / (float)in_height;
    }
    memset(out_buf, 0, out_buf_size);
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
#endif
static bool app_camera_has_pending_stage(void)
{
    bool has_pending = false;
    taskENTER_CRITICAL(&s_display_mux);
    has_pending = (s_pending_stage_index >= 0);
    taskEXIT_CRITICAL(&s_display_mux);
    return has_pending;
}
static int app_camera_pick_writable_stage_buffer(void)
{
    int idx = -1;
    taskENTER_CRITICAL(&s_display_mux);
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        if (s_stage_state[i] == DISP_BUF_FREE) {
            s_stage_state[i] = DISP_BUF_WRITING;
            idx = i;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_display_mux);
    return idx;
}
static void app_camera_publish_stage_buffer(int ready_index)
{
    taskENTER_CRITICAL(&s_display_mux);
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY) {
        s_stage_state[s_pending_stage_index] = DISP_BUF_FREE;
    }
    s_stage_state[ready_index] = DISP_BUF_READY;
    s_pending_stage_index = ready_index;
    taskEXIT_CRITICAL(&s_display_mux);
    if (s_display_task_handle) {
        xTaskNotifyGive(s_display_task_handle);
    }
}
static void app_camera_abandon_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS) {
        return;
    }
    taskENTER_CRITICAL(&s_display_mux);
    if (s_stage_state[buf_index] == DISP_BUF_WRITING) {
        s_stage_state[buf_index] = DISP_BUF_FREE;
    }
    taskEXIT_CRITICAL(&s_display_mux);
}
static int app_camera_take_ready_stage_buffer(void)
{
    int idx = -1;
    taskENTER_CRITICAL(&s_display_mux);
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY) {
        idx = s_pending_stage_index;
        s_pending_stage_index = -1;
    }
    taskEXIT_CRITICAL(&s_display_mux);
    return idx;
}
static void app_camera_release_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS) {
        return;
    }
    taskENTER_CRITICAL(&s_display_mux);
    s_stage_state[buf_index] = DISP_BUF_FREE;
    taskEXIT_CRITICAL(&s_display_mux);
}
static void app_camera_display_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (1) {
            int buf_index = app_camera_take_ready_stage_buffer();
            if (buf_index < 0 || s_camera_canvas == NULL || s_ui_canvas_buf == NULL) {
                break;
            }
            if (app_camera_msync_m2c(s_stage_buf[buf_index], s_disp_buf_size) != ESP_OK) {
                app_camera_release_stage_buffer(buf_index);
                continue;
            }
            if (bsp_display_lock(0)) {
                memcpy(s_ui_canvas_buf, s_stage_buf[buf_index], s_disp_buf_size);
                app_camera_msync_c2m(s_ui_canvas_buf, s_disp_buf_size);
                lv_obj_invalidate(s_camera_canvas);
                lv_refr_now(NULL);
                bsp_display_unlock();
            }
            app_camera_release_stage_buffer(buf_index);
        }
    }
}
static void app_camera_frame_cb(uint8_t *camera_buf,
                                uint8_t camera_buf_index,
                                uint32_t camera_buf_hes,
                                uint32_t camera_buf_ves,
                                size_t camera_buf_len)
{
    (void)camera_buf_index;
    if (camera_buf == NULL || s_camera_canvas == NULL || s_ui_canvas_buf == NULL) {
        return;
    }
    if (app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK) {
        return;
    }
    if (++s_vision_sample_skip >= VISION_SAMPLE_INTERVAL) {
        s_vision_sample_skip = 0;
        (void)app_vision_submit_frame(camera_buf,
                                      camera_buf_hes,
                                      camera_buf_ves,
                                      camera_buf_len);
    }
    if (app_camera_has_pending_stage()) {
        return;
    }
    int stage_index = app_camera_pick_writable_stage_buffer();
    if (stage_index < 0) {
        return;
    }
    uint32_t out_w = BSP_LCD_H_RES;
    uint32_t out_h = BSP_LCD_V_RES;
    uint8_t *out_buf = s_stage_buf[stage_index];
#if SOC_PPA_SUPPORTED
    if (s_ppa_srm_handle) {
        ppa_srm_rotation_angle_t rotation = PPA_SRM_ROTATION_ANGLE_0;
        switch (BSP_CAMERA_ROTATION) {
            case 90:  rotation = PPA_SRM_ROTATION_ANGLE_90; break;
            case 180: rotation = PPA_SRM_ROTATION_ANGLE_180; break;
            case 270: rotation = PPA_SRM_ROTATION_ANGLE_270; break;
            case 0:
            default:  rotation = PPA_SRM_ROTATION_ANGLE_0; break;
        }
        if (BSP_CAMERA_ROTATION == 90 || BSP_CAMERA_ROTATION == 270) {
            calc_aspect_fit(camera_buf_ves, camera_buf_hes, BSP_LCD_H_RES, BSP_LCD_V_RES, &out_w, &out_h);
        } else {
            calc_aspect_fit(camera_buf_hes, camera_buf_ves, BSP_LCD_H_RES, BSP_LCD_V_RES, &out_w, &out_h);
        }
        if (app_image_process_scale_crop(camera_buf,
                                         camera_buf_hes,
                                         camera_buf_ves,
                                         out_buf,
                                         out_w,
                                         out_h,
                                         s_disp_buf_size,
                                         rotation) != ESP_OK) {
            app_camera_abandon_stage_buffer(stage_index);
            return;
        }
    } else
#endif
    {
        size_t copy_len = camera_buf_len < s_disp_buf_size ? camera_buf_len : s_disp_buf_size;
        memcpy(out_buf, camera_buf, copy_len);
    }
    if (app_camera_msync_m2c(out_buf, s_disp_buf_size) != ESP_OK) {
        app_camera_abandon_stage_buffer(stage_index);
        return;
    }
    app_camera_publish_stage_buffer(stage_index);
}
static esp_err_t app_camera_start_display_task(void)
{
    if (s_display_task_handle) {
        return ESP_OK;
    }
    BaseType_t ret = xTaskCreatePinnedToCore(app_camera_display_task,
                                             "cam_display",
                                             DISPLAY_TASK_STACK_SIZE,
                                             NULL,
                                             DISPLAY_TASK_PRIORITY,
                                             &s_display_task_handle,
                                             1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
esp_err_t app_camera_init(void)
{
    if (s_camera_inited) {
        return ESP_OK;
    }
#if !BSP_CAPS_CAMERA
    ESP_LOGE(TAG, "this BSP does not support camera");
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_LOGI(TAG, "starting camera preview pipeline (USERPTR + stage queue + fixed canvas)");
    esp_err_t ret = bsp_camera_start(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_camera_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
#if SOC_PPA_SUPPORTED
    app_ppa_init();
#endif
    ret = app_camera_alloc_display_buffers();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = app_camera_create_canvas();
    if (ret != ESP_OK) {
        app_camera_free_display_buffers();
        return ret;
    }
    ret = app_camera_start_display_task();
    if (ret != ESP_OK) {
        app_camera_free_display_buffers();
        return ret;
    }
    ret = app_video_register_frame_operation_cb(app_camera_frame_cb);
    if (ret != ESP_OK) {
        app_camera_free_display_buffers();
        return ret;
    }
    s_camera_inited = true;
    return ESP_OK;
#endif
}
esp_err_t app_camera_preview_start(void)
{
    if (!s_camera_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_preview_running) {
        return ESP_OK;
    }
    s_video_fd = app_video_open(BSP_CAMERA_DEVICE, APP_VIDEO_FMT_RGB565);
    if (s_video_fd < 0) {
        ESP_LOGE(TAG, "app_video_open failed");
        ESP_LOGW(TAG, "please check camera sensor selection in menuconfig");
        return ESP_FAIL;
    }
    esp_err_t ret = app_camera_alloc_userptr_buffers(app_video_get_buf_size());
    if (ret != ESP_OK) {
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    ret = app_video_set_bufs(s_video_fd, CAMERA_NUM_BUFS, (const void **)s_cam_buf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "app_video_set_bufs(USERPTR) failed: %s", esp_err_to_name(ret));
        app_camera_free_camera_buffers();
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    ret = app_video_stream_task_start(s_video_fd, 0);
    if (ret != ESP_OK) {
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
esp_err_t app_camera_preview_stop(void)
{
    if (!s_preview_running) {
        return ESP_OK;
    }
    esp_err_t ret = app_video_stream_task_stop(s_video_fd);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "app_video_stream_task_stop failed: %s", esp_err_to_name(ret));
    }
    app_video_stream_wait_stop();
    close(s_video_fd);
    s_video_fd = -1;
    s_preview_running = false;
    app_camera_free_camera_buffers();
    return ESP_OK;
}
bool app_camera_is_preview_running(void)
{
    return s_preview_running;
}
