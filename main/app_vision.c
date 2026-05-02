/*
 * app_vision.c - 视觉识别任务调度模块（详细注释版）
 *
 * 这个文件位于 app_camera.c 和 app_apriltag.c 之间：
 * - app_camera.c 提交 RGB565 帧；
 * - 本文件把 RGB565 缩小/转成灰度图；
 * - 后台 FreeRTOS 任务调用 app_apriltag_detect_tag36h11()；
 * - 把识别结果缓存起来，并更新 UI 上的视觉文本。
 *
 * 设计重点是“不要卡住摄像头预览和 LVGL”：提交帧时只做轻量拷贝/转换，真正耗时的 AprilTag 检测放到后台任务里跑。
 */

#include "app_vision.h"                            // 项目自定义模块头文件，声明 app_vision 对外提供的接口。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "esp_timer.h"
#include "app_apriltag.h"                          // 项目自定义模块头文件，声明 app_apriltag 对外提供的接口。
#include "app_ai_capture.h"
#include "app_ui.h"                                // 项目自定义模块头文件，声明 app_ui 对外提供的接口。
#define VISION_TASK_STACK_SIZE         (16 * 1024)       // FreeRTOS 任务栈大小，单位一般是字节。
#define VISION_TASK_PRIORITY           4                 // FreeRTOS 任务优先级，数值越大优先级越高。
#define VISION_TASK_CORE_ID            1
#define VISION_POLL_PERIOD_MS          25
#define VISION_HEARTBEAT_MS            1000
#define VISION_GRAY_WIDTH              320               // 宽度相关参数，通常对应图像或显示尺寸。
#define VISION_GRAY_HEIGHT             240               // 高度相关参数，通常对应图像或显示尺寸。
#define VISION_GRAY_BUF_SIZE           (VISION_GRAY_WIDTH * VISION_GRAY_HEIGHT)
#define VISION_LOST_RESET_FRAMES       2U
#define VISION_STABLE_DECAY_ON_LOST    1U
static const char *TAG = "app_vision";                           // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    app_vision_gray_frame_info_t info;
    uint8_t gray[VISION_GRAY_BUF_SIZE];
} app_vision_gray_slot_t;
static TaskHandle_t s_vision_task = NULL;                        // 模块级静态变量 s_vision_task，只在本文件内部使用，避免被其他文件直接修改。
static bool s_vision_inited = false;                             // 模块级静态变量 s_vision_inited，只在本文件内部使用，避免被其他文件直接修改。
static portMUX_TYPE s_vision_mux = portMUX_INITIALIZER_UNLOCKED; // 模块级静态变量 s_vision_mux，只在本文件内部使用，避免被其他文件直接修改。
static app_vision_frame_info_t s_latest_frame = {0};             // 模块级静态变量 s_latest_frame，只在本文件内部使用，避免被其他文件直接修改。
static app_vision_gray_slot_t s_gray_slot = {0};                 // 模块级静态变量 s_gray_slot，只在本文件内部使用，避免被其他文件直接修改。
static app_vision_gray_slot_t s_task_slot = {0};                 // 模块级静态变量 s_task_slot，只在本文件内部使用，避免被其他文件直接修改。
static app_vision_gray_slot_t s_submit_slot = {0};               // 模块级静态变量 s_submit_slot，只在本文件内部使用，避免被其他文件直接修改。
static app_vision_result_t s_latest_result = {0};                // 模块级静态变量 s_latest_result，只在本文件内部使用，避免被其他文件直接修改。
static uint32_t s_submit_seq = 0;                                // 模块级静态变量 s_submit_seq，只在本文件内部使用，避免被其他文件直接修改。
static uint32_t s_submit_overwrite = 0;                          // 模块级静态变量 s_submit_overwrite，只在本文件内部使用，避免被其他文件直接修改。
static bool s_first_submit_logged = false;                       // 模块级静态变量 s_first_submit_logged，只在本文件内部使用，避免被其他文件直接修改。
static uint16_t s_sample_x[VISION_GRAY_WIDTH];
static uint16_t s_sample_y[VISION_GRAY_HEIGHT];
static uint32_t s_sample_map_width = 0;
static uint32_t s_sample_map_height = 0;

/*
 * 把 FreeRTOS tick 转成毫秒时间，用于检测耗时统计。
 */
static inline uint32_t app_vision_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/*
 * 把 RGB565 像素转换成灰度值，降低 AprilTag 检测计算量。
 */
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
/*
 * Prepare nearest-neighbor sampling maps once per source resolution.
 */
static void app_vision_prepare_sample_map(uint32_t width, uint32_t height)
{
    if (s_sample_map_width == width && s_sample_map_height == height) {
        return;
    }

    for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {
        uint32_t sx = (gx * width) / VISION_GRAY_WIDTH;
        if (sx >= width) {
            sx = width - 1U;
        }
        s_sample_x[gx] = (uint16_t)sx;
    }

    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {
        uint32_t sy = (gy * height) / VISION_GRAY_HEIGHT;
        if (sy >= height) {
            sy = height - 1U;
        }
        s_sample_y[gy] = (uint16_t)sy;
    }

    s_sample_map_width = width;
    s_sample_map_height = height;
}

/*
 * 在临界区中复制最新提交帧，避免后台任务读到一半被摄像头任务改写。
 */
static void app_vision_snapshot(app_vision_frame_info_t *meta_out,
                                app_vision_gray_slot_t *slot_out,
                                uint32_t *overwrite_out)
{
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_vision_mux);
    *meta_out = s_latest_frame;
    memcpy(slot_out, &s_gray_slot, sizeof(app_vision_gray_slot_t));
    *overwrite_out = s_submit_overwrite;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_vision_mux);
}
/*
 * 线程安全保存最新视觉识别结果。
 */
static void app_vision_store_result(const app_vision_result_t *result)
{
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_vision_mux);
    s_latest_result = *result;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_vision_mux);
}
/*
 * 读取最新视觉识别结果，供接驳判定模块使用。
 */
bool app_vision_get_latest_result(app_vision_result_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (out == NULL) {
        return false;
    }
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_vision_mux);
    *out = s_latest_result;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_vision_mux);
    return out->valid;
}
/*
 * 在 UI 上显示视觉任务仍在等待有效图像帧。
 */
static void app_vision_set_wait_text(uint32_t heartbeat)
{
    if (app_ai_capture_is_active()) {
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "tag: wait #%lu", (unsigned long)heartbeat);
    app_ui_set_vision_text(buf);
}
/*
 * 把 AprilTag 检测结果转换成项目统一的 app_vision_result_t，并更新稳定/丢失计数。
 */
static void app_vision_update_result(const app_vision_gray_slot_t *slot,
                                     const app_apriltag_result_t *tag,
                                     uint32_t detect_ms,
                                     uint16_t *stable_count,
                                     uint16_t *lost_count,
                                     uint16_t *last_tag_id)
{
    app_vision_result_t result = {0};
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (tag != NULL && tag->valid) {
        /*
         * 连续稳定计数。
         *
         * 如果当前帧识别到的 tag ID 和上一帧相同，就累加 stable_count；
         * 如果 ID 变化，说明跟踪目标发生切换，从 1 重新开始。
         */
        if (*last_tag_id == tag->id) {
            if (*stable_count < UINT16_MAX) {
                (*stable_count)++;
            }
        } else {
            *stable_count = 1;
            *last_tag_id = tag->id;
        }
        *lost_count = 0;

        /*
         * 把 app_apriltag.c 的底层识别结果转换成 app_vision_result_t。
         *
         * app_vision_result_t 是视觉模块对外输出的统一格式，
         * 后续 app_dock_judge.c 和 app_ui.c 都只依赖这个结构。
         */
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

        /*
         * 保存最新结果，供控制任务异步读取。
         */
        app_vision_store_result(&result);

        /*
         * 更新 UI 上的简短视觉状态。
         */
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "id:%u hm:%u st:%u e:%.1f ang:%d",
                 (unsigned)result.tag_id,
                 (unsigned)result.hamming,
                 (unsigned)result.stable_count,
                 (double)result.edge_px_avg,
                 (int)result.top_edge_angle_deg);
        if (!app_ai_capture_is_active()) {
            app_ui_set_vision_text(buf);
        }
        // 信息日志：用于确认程序执行到了哪个阶段。
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

    /*
     * 没有识别到 tag 的路径。
     *
     * lost_count 用来记录连续丢失帧数；
     * stable_count 不会马上清零，而是先按 VISION_STABLE_DECAY_ON_LOST 衰减，
     * 这样短暂漏检不会立刻把接驳判定打回零稳定度。
     */
    if (*lost_count < UINT16_MAX) {
        (*lost_count)++;
    }
    if ((*lost_count < VISION_LOST_RESET_FRAMES) && (*stable_count > VISION_STABLE_DECAY_ON_LOST)) {
        *stable_count = (uint16_t)(*stable_count - VISION_STABLE_DECAY_ON_LOST);
    } else if (*lost_count >= VISION_LOST_RESET_FRAMES) {
        *stable_count = 0;
        *last_tag_id = 0;
    }

    /*
     * 输出一帧 valid=false 的结果。
     *
     * 即使没有 tag，也要更新 frame_seq / detect_ms / lost_count，
     * 这样 app_dock_judge.c 能知道当前是“没有新帧”还是“新帧确实没识别到 tag”。
     */
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
    if (!app_ai_capture_is_active()) {
        app_ui_set_vision_text(buf);
    }
}
/*
 * 视觉后台任务，持续获取最新灰度帧并调用 AprilTag 检测。
 */
static void app_vision_task(void *arg)
{
    /*
     * 本任务没有使用外部参数。
     */
    (void)arg;

    /*
     * heartbeat 用于在长时间没有新帧时更新 UI，证明视觉任务本身仍然活着。
     * last_seq 用于避免重复处理同一帧。
     * last_overwrite 用于统计提交帧被覆盖的情况。
     */
    uint32_t heartbeat = 0;
    uint32_t last_seq = 0;
    uint32_t last_overwrite = 0;
    TickType_t last_heartbeat_tick = xTaskGetTickCount();

    /*
     * 识别稳定性状态。
     *
     * 这些计数保存在视觉任务本地，而不是 app_apriltag.c 中，
     * 因为 AprilTag 检测只负责单帧识别，不负责跨帧状态。
     */
    uint16_t stable_count = 0;
    uint16_t lost_count = 0;
    uint16_t last_tag_id = 0;

    if (!app_ai_capture_is_active()) {
        app_ui_set_vision_text("tag: wait frame");
    }

    while (1) {
        app_vision_frame_info_t meta;
        uint32_t overwrite = 0;

        /*
         * 快照最新灰度帧。
         *
         * app_camera.c 可能随时提交新帧，所以这里通过临界区复制一份到 s_task_slot，
         * 让 AprilTag 检测期间不会被摄像头任务改写。
         */
        app_vision_snapshot(&meta, &s_task_slot, &overwrite);

        /*
         * 发现新的灰度帧：执行 AprilTag 检测。
         */
        if (s_task_slot.info.seq != 0 && s_task_slot.info.seq != last_seq) {
            int64_t start_us = esp_timer_get_time();
            app_apriltag_result_t tag = {0};

            /*
             * 真正耗时的识别在后台任务里执行，避免阻塞摄像头预览回调。
             */
            bool found = app_apriltag_detect_tag36h11(s_task_slot.gray,
                                                      s_task_slot.info.gray_width,
                                                      s_task_slot.info.gray_height,
                                                      &tag);
            uint32_t detect_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL);

            /*
             * 不管 found 与否，都通过 app_vision_update_result() 统一更新稳定计数和 UI。
             */
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

            /*
             * 如果 seq 跳号，说明视觉任务处理速度跟不上摄像头提交速度，
             * 中间有帧被覆盖或跳过。这不是致命错误，但会影响识别连续性。
             */
            if (last_seq != 0 && s_task_slot.info.seq > (last_seq + 1)) {
                // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
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
            /*
             * 有新的原始帧元信息，但灰度 slot 还没准备好或没有新识别结果。
             * 这里给 UI 一个轻量反馈，避免一直停留在旧文本。
             */
            char buf[64];
            snprintf(buf,
                     sizeof(buf),
                     "frame#%lu %lux%lu",
                     (unsigned long)meta.seq,
                     (unsigned long)meta.width,
                     (unsigned long)meta.height);
            if (!app_ai_capture_is_active()) {
                app_ui_set_vision_text(buf);
            }
            last_seq = meta.seq;
            last_heartbeat_tick = xTaskGetTickCount();
        } else {
            /*
             * 没有新帧时，周期性刷新等待文本。
             */
            TickType_t now = xTaskGetTickCount();
            if ((now - last_heartbeat_tick) >= pdMS_TO_TICKS(VISION_HEARTBEAT_MS)) {
                heartbeat++;
                app_vision_set_wait_text(heartbeat);
                last_heartbeat_tick = now;
            }
        }
        // 等待新帧通知；超时醒来只用于刷新等待状态。
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VISION_POLL_PERIOD_MS));
    }
}
/*
 * 初始化视觉模块状态和 AprilTag 底层缓冲区。
 */
esp_err_t app_vision_init(void)
{
    if (s_vision_inited) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    esp_err_t ret = app_apriltag_init();
    if (ret != ESP_OK) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "app_apriltag_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_vision_mux);
    memset(&s_latest_frame, 0, sizeof(s_latest_frame));
    memset(&s_gray_slot, 0, sizeof(s_gray_slot));
    memset(&s_task_slot, 0, sizeof(s_task_slot));
    memset(&s_submit_slot, 0, sizeof(s_submit_slot));
    memset(&s_latest_result, 0, sizeof(s_latest_result));
    s_submit_seq = 0;
    s_submit_overwrite = 0;
    s_first_submit_logged = false;
    s_sample_map_width = 0;
    s_sample_map_height = 0;
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_vision_mux);
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "vision init done, gray=%dx%d", VISION_GRAY_WIDTH, VISION_GRAY_HEIGHT);
    s_vision_inited = true;
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 创建视觉任务，开始后台识别。
 */
esp_err_t app_vision_start(void)
{
    if (!s_vision_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_vision_task != NULL) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    // 创建并固定 FreeRTOS 任务到指定 CPU 核，减少任务迁移带来的抖动。
    BaseType_t ret = xTaskCreatePinnedToCore(app_vision_task,
                                             "app_vision",
                                             VISION_TASK_STACK_SIZE,
                                             NULL,
                                             VISION_TASK_PRIORITY,
                                             &s_vision_task,
                                             VISION_TASK_CORE_ID);
    if (ret != pdPASS) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "create vision task failed");
        s_vision_task = NULL;
        return ESP_FAIL;
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "vision task started");
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 摄像头模块提交 RGB565 帧，本函数抽样/缩放/转灰度后交给视觉任务处理。
 */
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
                                  uint32_t width,
                                  uint32_t height,
                                  size_t len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (rgb565 == NULL || width == 0 || height == 0) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
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
    app_vision_prepare_sample_map(width, height);
    const uint16_t *src = (const uint16_t *)rgb565;
    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {
        const uint16_t *src_row = src + (size_t)s_sample_y[gy] * width;
        uint8_t *dst_row = &s_submit_slot.gray[gy * VISION_GRAY_WIDTH];
        for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {
            uint16_t pixel = src_row[s_sample_x[gx]];
            dst_row[gx] = app_rgb565_to_gray(pixel);
        }
    }
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
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
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_vision_mux);
    if (s_vision_task != NULL) {
        xTaskNotifyGive(s_vision_task);
    }

    if (!s_first_submit_logged) {
        // 信息日志：用于确认程序执行到了哪个阶段。
        ESP_LOGI(TAG,
                 "first gray frame ready: src=%lux%lu gray=%dx%d len=%lu",
                 (unsigned long)width,
                 (unsigned long)height,
                 VISION_GRAY_WIDTH,
                 VISION_GRAY_HEIGHT,
                 (unsigned long)len);
        s_first_submit_logged = true;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
