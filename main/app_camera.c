/*
 * app_camera.c - 摄像头采集、本地预览和视觉取样桥接模块（详细注释版）
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

#include "app_camera.h"                            // 项目自定义模块头文件，声明 app_camera 对外提供的接口。
#include <stdbool.h>                               // C99 布尔类型支持，提供 bool、true、false。
#include <stdint.h>                                // 固定宽度整数类型，例如 uint8_t、uint16_t、uint32_t，嵌入式代码常用。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
#include <unistd.h>                                // POSIX 风格接口，例如 close/read/write，在 V4L2 设备操作中常用。
#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include "sdkconfig.h"                             // ESP-IDF menuconfig 生成的配置头文件。
#include "esp_err.h"                               // ESP-IDF 错误码类型 esp_err_t 和 ESP_OK 等定义。
#include "esp_heap_caps.h"                         // 带内存能力属性的堆分配接口，例如申请 PSRAM/DMA 可访问内存。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "esp_cache.h"                             // Cache 同步接口，解决 CPU、DMA、PPA 访问 PSRAM 时的数据一致性问题。
#include "esp_private/esp_cache_private.h"         // ESP-IDF 内部 cache 辅助接口，用于获取对齐要求。
#include "lvgl.h"                                  // LVGL 图形库主头文件，提供控件、样式、画布、颜色等 API。
#include "bsp/esp-bsp.h"                           // 乐鑫 BSP 通用接口，常用于显示、触摸、音频等板级资源。
#include "bsp/display.h"                           // BSP 显示接口和分辨率宏，例如 BSP_LCD_H_RES/BSP_LCD_V_RES。
#include "app_video.h"                             // 项目自定义模块头文件，声明 app_video 对外提供的接口。
#include "app_vision.h"                            // 项目自定义模块头文件，声明 app_vision 对外提供的接口。
#if SOC_PPA_SUPPORTED
#include "driver/ppa.h"                            // ESP32-P4 PPA 图像处理外设接口，用于缩放/旋转/镜像等图像处理。
#endif
#define CAMERA_NUM_BUFS          4                       // 缓冲区数量，通常用于摄像头多缓冲或显示 stage buffer。
#define STAGE_NUM_BUFS           3                       // 缓冲区数量，通常用于摄像头多缓冲或显示 stage buffer。
#define ALIGN_UP(num, align)     (((num) + ((align) - 1)) & ~((align) - 1))
#define DISPLAY_TASK_STACK_SIZE  (6 * 1024)              // FreeRTOS 任务栈大小，单位一般是字节。
#define DISPLAY_TASK_PRIORITY    7                       // FreeRTOS 任务优先级，数值越大优先级越高。
#define VISION_SAMPLE_INTERVAL   6                       // 周期/间隔参数，用于控制采样或刷新频率。
static const char *TAG = "app_camera";                           // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
static bool s_camera_inited = false;                             // 模块级静态变量 s_camera_inited，只在本文件内部使用，避免被其他文件直接修改。
static bool s_preview_running = false;                           // 模块级静态变量 s_preview_running，只在本文件内部使用，避免被其他文件直接修改。
static int s_video_fd = -1;                                      // 模块级静态变量 s_video_fd，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_camera_canvas = NULL;                         // 模块级静态变量 s_camera_canvas，只在本文件内部使用，避免被其他文件直接修改。
static size_t s_cache_line_size = 0;                             // 模块级静态变量 s_cache_line_size，只在本文件内部使用，避免被其他文件直接修改。
static uint8_t *s_cam_buf[CAMERA_NUM_BUFS] = {0};                // 模块级静态变量 s_cam_buf，只在本文件内部使用，避免被其他文件直接修改。
static size_t s_cam_buf_size = 0;                                // 模块级静态变量 s_cam_buf_size，只在本文件内部使用，避免被其他文件直接修改。
static uint8_t *s_stage_buf[STAGE_NUM_BUFS] = {0};               // 模块级静态变量 s_stage_buf，只在本文件内部使用，避免被其他文件直接修改。
static uint8_t *s_ui_canvas_buf = NULL;                          // 模块级静态变量 s_ui_canvas_buf，只在本文件内部使用，避免被其他文件直接修改。
static uint32_t s_disp_buf_size = 0;                             // 模块级静态变量 s_disp_buf_size，只在本文件内部使用，避免被其他文件直接修改。
static TaskHandle_t s_display_task_handle = NULL;                // 模块级静态变量 s_display_task_handle，只在本文件内部使用，避免被其他文件直接修改。
static portMUX_TYPE s_display_mux = portMUX_INITIALIZER_UNLOCKED; // 模块级静态变量 s_display_mux，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 枚举类型：用一组有名字的常量表示状态/类型，比直接写数字更清晰，也方便调试。
 */
typedef enum {
    DISP_BUF_FREE = 0,
    DISP_BUF_WRITING,
    DISP_BUF_READY,
} disp_buf_state_t;
static volatile int s_pending_stage_index = -1;                  // 模块级静态变量 s_pending_stage_index，只在本文件内部使用，避免被其他文件直接修改。
static uint32_t s_vision_sample_skip = 0;                        // 模块级静态变量 s_vision_sample_skip，只在本文件内部使用，避免被其他文件直接修改。
static disp_buf_state_t s_stage_state[STAGE_NUM_BUFS] = {0};     // 模块级静态变量 s_stage_state，只在本文件内部使用，避免被其他文件直接修改。
#if SOC_PPA_SUPPORTED
static ppa_client_handle_t s_ppa_srm_handle = NULL;              // 模块级静态变量 s_ppa_srm_handle，只在本文件内部使用，避免被其他文件直接修改。
#endif
/*
 * 按 cache line 对齐后执行 cache 同步，避免 DMA/PPA/CPU 看到不同版本的 PSRAM 数据。
 */
static esp_err_t app_camera_msync_aligned(void *addr, size_t size, int flags)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (addr == NULL || size == 0) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
    size_t align = s_cache_line_size ? s_cache_line_size : 64;
    uintptr_t start = (uintptr_t)addr & ~((uintptr_t)align - 1U);
    uintptr_t end = ((uintptr_t)addr + size + align - 1U) & ~((uintptr_t)align - 1U);
    // 同步 cache 和内存，保证 CPU 与 DMA/PPA 对同一缓冲区看到一致数据。
    return esp_cache_msync((void *)start, end - start, flags);
}
/*
 * 内存到 cache 方向同步，通常在硬件写完内存后让 CPU 读取到最新数据。
 */
static inline esp_err_t app_camera_msync_m2c(const void *addr, size_t size)
{
    return app_camera_msync_aligned((void *)addr, size,
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C |
                                    ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}
/*
 * cache 到内存方向同步，通常在 CPU 写完缓冲区后让硬件读取到最新数据。
 */
static inline esp_err_t app_camera_msync_c2m(void *addr, size_t size)
{
    return app_camera_msync_aligned(addr, size,
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                                    ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}
/*
 * 兼容 LVGL v8/v9 获取当前活动屏幕，避免工程切换 LVGL 版本时到处改代码。
 */
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
/*
 * 释放摄像头 USERPTR 帧缓冲区，防止初始化失败或停止预览后内存泄漏。
 */
static void app_camera_free_camera_buffers(void)
{
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {
        if (s_cam_buf[i]) {
            // 释放 heap_caps 分配的缓冲区。
            heap_caps_free(s_cam_buf[i]);
            s_cam_buf[i] = NULL;
        }
    }
    s_cam_buf_size = 0;
}
/*
 * 释放显示 stage buffer 和 LVGL canvas buffer，同时重置缓冲状态机。
 */
static void app_camera_free_display_buffers(void)
{
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        if (s_stage_buf[i]) {
            // 释放 heap_caps 分配的缓冲区。
            heap_caps_free(s_stage_buf[i]);
            s_stage_buf[i] = NULL;
        }
        s_stage_state[i] = DISP_BUF_FREE;
    }
    if (s_ui_canvas_buf) {
        // 释放 heap_caps 分配的缓冲区。
        heap_caps_free(s_ui_canvas_buf);
        s_ui_canvas_buf = NULL;
    }
    s_pending_stage_index = -1;
    s_disp_buf_size = 0;
}
/*
 * 在 PSRAM 中分配显示用缓冲区，包括一个 LVGL 画布缓冲和多个后台 stage 缓冲。
 */
static esp_err_t app_camera_alloc_display_buffers(void)
{
    esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
    if (ret != ESP_OK) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_disp_buf_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_line_size);
    // 按指定对齐和内存能力申请缓冲区，适合 PSRAM/DMA/cache 场景。
    s_ui_canvas_buf = heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_ui_canvas_buf == NULL) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "alloc ui canvas buffer failed");
        // 内存不足是嵌入式项目常见问题，这里返回错误让上层决定是否停止初始化。
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        // 按指定对齐和内存能力申请缓冲区，适合 PSRAM/DMA/cache 场景。
        s_stage_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM);
        // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
        if (s_stage_buf[i] == NULL) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "alloc stage buffer %d failed", i);
            app_camera_free_display_buffers();
            // 内存不足是嵌入式项目常见问题，这里返回错误让上层决定是否停止初始化。
            return ESP_ERR_NO_MEM;
        }
        s_stage_state[i] = DISP_BUF_FREE;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 为 V4L2 USERPTR 模式分配摄像头帧缓冲，让驱动直接把图像写到用户提供的 PSRAM。
 */
static esp_err_t app_camera_alloc_userptr_buffers(size_t frame_size)
{
    if (s_cache_line_size == 0) {
        esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);
        if (ret != ESP_OK) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    s_cam_buf_size = ALIGN_UP(frame_size, s_cache_line_size);
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {
        // 按指定对齐和内存能力申请缓冲区，适合 PSRAM/DMA/cache 场景。
        s_cam_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, s_cam_buf_size, MALLOC_CAP_SPIRAM);
        // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
        if (s_cam_buf[i] == NULL) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "alloc USERPTR camera buffer %d failed", i);
            app_camera_free_camera_buffers();
            // 内存不足是嵌入式项目常见问题，这里返回错误让上层决定是否停止初始化。
            return ESP_ERR_NO_MEM;
        }
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "USERPTR camera buffers ready: %d x %u bytes", CAMERA_NUM_BUFS, (unsigned)s_cam_buf_size);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 在 LVGL 当前屏幕上创建摄像头画布，并绑定 RGB565 显示缓冲。
 */
static esp_err_t app_camera_create_canvas(void)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_camera_canvas != NULL) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    if (!bsp_display_lock(0)) {
        return ESP_FAIL;
    }
    lv_obj_t *scr = app_get_active_screen();
    s_camera_canvas = lv_canvas_create(scr);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_camera_canvas == NULL) {
        // 释放 LVGL/BSP 显示锁。
        bsp_display_unlock();
        return ESP_FAIL;
    }
#if LVGL_VERSION_MAJOR >= 9
    // 把内存缓冲区绑定到 LVGL canvas，后续刷新 canvas 就能显示图像。
    lv_canvas_set_buffer(s_camera_canvas, s_ui_canvas_buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_COLOR_FORMAT_RGB565);
#else
    // 把内存缓冲区绑定到 LVGL canvas，后续刷新 canvas 就能显示图像。
    lv_canvas_set_buffer(s_camera_canvas, s_ui_canvas_buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
#endif
    lv_obj_center(s_camera_canvas);
    lv_obj_move_background(s_camera_canvas);
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
#if SOC_PPA_SUPPORTED
/*
 * 注册 ESP32-P4 PPA 图像处理客户端，用硬件加速完成缩放/旋转。
 */
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
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "ppa_register_client failed: 0x%x", ret);
    }
}
/*
 * 计算等比例缩放后的显示尺寸，保证摄像头画面不被拉伸变形。
 */
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
/*
 * 使用 PPA 把摄像头 RGB565 图像缩放/旋转到屏幕大小，供 LVGL 画布显示。
 */
static esp_err_t app_image_process_scale_crop(uint8_t *in_buf,
                                              uint32_t in_width,
                                              uint32_t in_height,
                                              uint8_t *out_buf,
                                              uint32_t out_width,
                                              uint32_t out_height,
                                              size_t out_buf_size,
                                              ppa_srm_rotation_angle_t rotation_angle)
{
    /*
     * PPA 的缩放比例使用“输出尺寸 / 输入尺寸”。
     * 普通 0/180 度旋转时，宽对应宽、高对应高。
     */
    float scale_x = (float)out_width / (float)in_width;
    float scale_y = (float)out_height / (float)in_height;

    /*
     * 90/270 度旋转后，输入宽度会落到输出高度方向，
     * 输入高度会落到输出宽度方向，所以这里需要交换比例计算方式。
     */
    if (rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || rotation_angle == PPA_SRM_ROTATION_ANGLE_270) {
        scale_x = (float)out_height / (float)in_width;
        scale_y = (float)out_width / (float)in_height;
    }

    /*
     * 先清空输出缓冲。
     *
     * 等比例适配时会出现黑边，清零可以避免上一帧的边缘残影留在屏幕上。
     */
    memset(out_buf, 0, out_buf_size);

    /*
     * PPA SRM 配置：
     * - in 描述摄像头输入图；
     * - out 描述屏幕画布输出图；
     * - block_offset_x/y 用于居中显示；
     * - BLOCKING 模式保证函数返回时处理已经完成。
     */
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

    /*
     * 执行硬件缩放/旋转/镜像处理。
     */
    return ppa_do_scale_rotate_mirror(s_ppa_srm_handle, &srm_cfg);
}
#endif
/*
 * 判断是否已有待显示的 stage buffer，避免显示任务重复等待。
 */
static bool app_camera_has_pending_stage(void)
{
    bool has_pending = false;
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_display_mux);
    has_pending = (s_pending_stage_index >= 0);
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_display_mux);
    return has_pending;
}
/*
 * 从 stage buffer 池中挑一个空闲缓冲给图像处理任务写入。
 */
static int app_camera_pick_writable_stage_buffer(void)
{
    int idx = -1;
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_display_mux);
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {
        if (s_stage_state[i] == DISP_BUF_FREE) {
            s_stage_state[i] = DISP_BUF_WRITING;
            idx = i;
            break;
        }
    }
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_display_mux);
    return idx;
}
/*
 * 把刚写好的 stage buffer 标记为 READY，交给显示任务消费。
 */
static void app_camera_publish_stage_buffer(int ready_index)
{
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_display_mux);
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY) {
        s_stage_state[s_pending_stage_index] = DISP_BUF_FREE;
    }
    s_stage_state[ready_index] = DISP_BUF_READY;
    s_pending_stage_index = ready_index;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_display_mux);
    if (s_display_task_handle) {
        xTaskNotifyGive(s_display_task_handle);
    }
}
/*
 * 当处理失败或预览停止时放弃某个 stage buffer，重新标记为空闲。
 */
static void app_camera_abandon_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS) {
        return;
    }
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_display_mux);
    if (s_stage_state[buf_index] == DISP_BUF_WRITING) {
        s_stage_state[buf_index] = DISP_BUF_FREE;
    }
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_display_mux);
}
/*
 * 显示任务取出最新 READY 缓冲；如果旧帧来不及显示，会优先显示最新帧。
 */
static int app_camera_take_ready_stage_buffer(void)
{
    int idx = -1;
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_display_mux);
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY) {
        idx = s_pending_stage_index;
        s_pending_stage_index = -1;
    }
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_display_mux);
    return idx;
}
/*
 * 显示任务把缓冲拷贝到 LVGL canvas 后释放 stage buffer。
 */
static void app_camera_release_stage_buffer(int buf_index)
{
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS) {
        return;
    }
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_display_mux);
    s_stage_state[buf_index] = DISP_BUF_FREE;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_display_mux);
}
/*
 * 独立显示任务，负责取摄像头帧、图像缩放、更新 LVGL canvas，并按间隔提交视觉识别。
 */
static void app_camera_display_task(void *arg)
{
    /*
     * 显示任务不需要外部参数。
     */
    (void)arg;
    while (1) {
        /*
         * 等待摄像头回调发布新的 stage buffer。
         * 没有新帧时任务阻塞，避免空转占用 CPU。
         */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (1) {
            /*
             * 取出最新 READY 缓冲。
             * 如果没有待显示帧，结束本轮消费，回到通知等待状态。
             */
            int buf_index = app_camera_take_ready_stage_buffer();
            // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
            if (buf_index < 0 || s_camera_canvas == NULL || s_ui_canvas_buf == NULL) {
                break;
            }

            /*
             * stage buffer 可能刚被 PPA/DMA 写过。
             * CPU 拷贝前先做 M2C 同步，确保读到的是最新图像数据。
             */
            if (app_camera_msync_m2c(s_stage_buf[buf_index], s_disp_buf_size) != ESP_OK) {
                app_camera_release_stage_buffer(buf_index);
                continue;
            }
            // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
            if (bsp_display_lock(0)) {
                /*
                 * LVGL canvas 绑定的是 s_ui_canvas_buf。
                 * stage buffer 是后台缓冲，处理完成后需要复制到 canvas buffer 再刷新控件。
                 */
                memcpy(s_ui_canvas_buf, s_stage_buf[buf_index], s_disp_buf_size);
                app_camera_msync_c2m(s_ui_canvas_buf, s_disp_buf_size);
                lv_obj_invalidate(s_camera_canvas);
                lv_refr_now(NULL);
                // 释放 LVGL/BSP 显示锁。
                bsp_display_unlock();
            }

            /*
             * 释放 stage buffer。
             * 即使本轮没有拿到 LVGL 锁，也不能一直占着缓冲，否则摄像头回调会越来越容易丢帧。
             */
            app_camera_release_stage_buffer(buf_index);
        }
    }
}
/*
 * 摄像头帧回调。
 *
 * app_video.c 每取到一帧 RGB565 图像就会调用这里。
 * 本函数负责把同一帧分发到两条链路：
 * - 抽样提交给 app_vision.c 做 AprilTag 识别；
 * - 缩放/旋转后发布给显示任务刷新 LVGL canvas。
 */
static void app_camera_frame_cb(uint8_t *camera_buf,
                                uint8_t camera_buf_index,
                                uint32_t camera_buf_hes,
                                uint32_t camera_buf_ves,
                                size_t camera_buf_len)
{
    /*
     * 当前没有使用 camera_buf_index。
     * USERPTR 模式下缓冲地址已经能表示当前帧来源。
     */
    (void)camera_buf_index;

    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (camera_buf == NULL || s_camera_canvas == NULL || s_ui_canvas_buf == NULL) {
        return;
    }

    /*
     * 摄像头 DMA/驱动刚写完 camera_buf，CPU 读取前需要同步 cache。
     */
    if (app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK) {
        return;
    }

    /*
     * 视觉识别不必每帧都跑。
     * 抽样可以减少 AprilTag 检测负载，让摄像头预览更流畅。
     */
    if (++s_vision_sample_skip >= VISION_SAMPLE_INTERVAL) {
        s_vision_sample_skip = 0;
        (void)app_vision_submit_frame(camera_buf,
                                      camera_buf_hes,
                                      camera_buf_ves,
                                      camera_buf_len);
    }

    /*
     * 如果已经有一帧待显示，就跳过当前帧的显示处理。
     * 这能避免显示任务慢时不断堆积旧帧。
     */
    if (app_camera_has_pending_stage()) {
        return;
    }

    /*
     * 从 stage buffer 池里取一个空闲缓冲。
     */
    int stage_index = app_camera_pick_writable_stage_buffer();
    if (stage_index < 0) {
        return;
    }

    uint32_t out_w = BSP_LCD_H_RES;
    uint32_t out_h = BSP_LCD_V_RES;
    uint8_t *out_buf = s_stage_buf[stage_index];
#if SOC_PPA_SUPPORTED
    if (s_ppa_srm_handle) {
        /*
         * 把 BSP_CAMERA_ROTATION 转成 PPA 使用的旋转枚举。
         */
        ppa_srm_rotation_angle_t rotation = PPA_SRM_ROTATION_ANGLE_0;
        switch (BSP_CAMERA_ROTATION) {
            case 90:  rotation = PPA_SRM_ROTATION_ANGLE_90; break;
            case 180: rotation = PPA_SRM_ROTATION_ANGLE_180; break;
            case 270: rotation = PPA_SRM_ROTATION_ANGLE_270; break;
            case 0:
            default:  rotation = PPA_SRM_ROTATION_ANGLE_0; break;
        }

        /*
         * 根据旋转后的源图宽高，计算适配屏幕的显示尺寸。
         */
        if (BSP_CAMERA_ROTATION == 90 || BSP_CAMERA_ROTATION == 270) {
            calc_aspect_fit(camera_buf_ves, camera_buf_hes, BSP_LCD_H_RES, BSP_LCD_V_RES, &out_w, &out_h);
        } else {
            calc_aspect_fit(camera_buf_hes, camera_buf_ves, BSP_LCD_H_RES, BSP_LCD_V_RES, &out_w, &out_h);
        }

        /*
         * 用 PPA 生成最终显示帧。
         * 失败时必须归还 stage buffer，避免缓冲状态机卡住。
         */
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
        /*
         * 没有 PPA 的降级路径。
         * 只做安全长度的直接拷贝，保证工程至少能看到原始画面数据。
         */
        size_t copy_len = camera_buf_len < s_disp_buf_size ? camera_buf_len : s_disp_buf_size;
        memcpy(out_buf, camera_buf, copy_len);
    }

    /*
     * out_buf 接下来由显示任务读取。
     * 发布前同步一次，保证跨任务读取到完整图像。
     */
    if (app_camera_msync_m2c(out_buf, s_disp_buf_size) != ESP_OK) {
        app_camera_abandon_stage_buffer(stage_index);
        return;
    }

    /*
     * 标记为 READY，并通知显示任务消费。
     */
    app_camera_publish_stage_buffer(stage_index);
}
/*
 * 创建摄像头显示任务，确保预览逻辑运行在独立 FreeRTOS 任务中。
 */
static esp_err_t app_camera_start_display_task(void)
{
    if (s_display_task_handle) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    // 创建并固定 FreeRTOS 任务到指定 CPU 核，减少任务迁移带来的抖动。
    BaseType_t ret = xTaskCreatePinnedToCore(app_camera_display_task,
                                             "cam_display",
                                             DISPLAY_TASK_STACK_SIZE,
                                             NULL,
                                             DISPLAY_TASK_PRIORITY,
                                             &s_display_task_handle,
                                             1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
/*
 * 初始化摄像头模块资源，包括显示缓冲、canvas、PPA 和 V4L2 设备。
 */
esp_err_t app_camera_init(void)
{
    if (s_camera_inited) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
#if !BSP_CAPS_CAMERA
    // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
    ESP_LOGE(TAG, "this BSP does not support camera");
    return ESP_ERR_NOT_SUPPORTED;
#else
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "starting camera preview pipeline (USERPTR + stage queue + fixed canvas)");
    esp_err_t ret = bsp_camera_start(NULL);
    if (ret != ESP_OK) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
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
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
#endif
}
/*
 * 启动摄像头视频流和显示任务，让屏幕开始显示实时画面。
 */
esp_err_t app_camera_preview_start(void)
{
    if (!s_camera_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_preview_running) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    s_video_fd = app_video_open(BSP_CAMERA_DEVICE, APP_VIDEO_FMT_RGB565);
    if (s_video_fd < 0) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "app_video_open failed");
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
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
/*
 * 把上层分配的帧缓冲注册给 V4L2 驱动，使用 USERPTR 模式接收图像。
 */
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "app_video_set_bufs(USERPTR) failed: %s", esp_err_to_name(ret));
        app_camera_free_camera_buffers();
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    ret = app_video_stream_task_start(s_video_fd, 0);
    if (ret != ESP_OK) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "app_video_stream_task_start failed: %s", esp_err_to_name(ret));
        app_camera_free_camera_buffers();
        close(s_video_fd);
        s_video_fd = -1;
        return ret;
    }
    s_preview_running = true;
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "camera preview started");
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
