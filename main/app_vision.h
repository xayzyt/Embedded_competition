#pragma once

/*
 * 视觉工作任务接口。
 * 摄像头帧从这里进入，最新 AprilTag 识别结果从这里读出。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 从 app_apriltag 拷贝出的识别结果，以及源帧元数据。 */
    bool valid;
    uint16_t tag_id;
    uint8_t hamming;
    uint8_t rotation;
    uint8_t threshold;
    uint8_t border_dark_pct;
    int32_t center_x;
    int32_t center_y;
    int32_t area;
    int32_t bbox_x;
    int32_t bbox_y;
    int32_t bbox_w;
    int32_t bbox_h;

    int32_t corner_tl_x;
    int32_t corner_tl_y;
    int32_t corner_tr_x;
    int32_t corner_tr_y;
    int32_t corner_br_x;
    int32_t corner_br_y;
    int32_t corner_bl_x;
    int32_t corner_bl_y;

    float edge_px_avg;
    float top_edge_angle_deg;

    uint32_t src_width;
    uint32_t src_height;
    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    uint32_t gray_width;
    uint32_t gray_height;

    uint32_t frame_seq;
    uint32_t detect_ms;
    uint16_t stable_count;
    uint16_t lost_count;
} app_vision_result_t;

typedef struct {
    /* app_camera 提交的 RGB565 帧基础元数据。 */
    uint32_t width;
    uint32_t height;
    size_t len;
    uint32_t seq;
    uint32_t tick_ms;
} app_vision_frame_info_t;

typedef struct {
    /* 检测器实际使用的内部灰度裁剪图元数据。 */
    uint32_t src_width;
    uint32_t src_height;
    uint32_t gray_width;
    uint32_t gray_height;
    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    size_t gray_len;
    uint32_t seq;
    uint32_t tick_ms;
} app_vision_gray_frame_info_t;

/* 初始化 AprilTag 检测器和视觉任务内部缓冲区。 */
esp_err_t app_vision_init(void);

/* 创建后台视觉检测任务。 */
esp_err_t app_vision_start(void);

/* 非阻塞式提交 RGB565 帧，耗时检测在后台任务中执行。 */
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
                                  uint32_t width,
                                  uint32_t height,
                                  size_t len);

/* 读取最近一次稳定的检测结果。 */
bool app_vision_get_latest_result(app_vision_result_t *out);

#ifdef __cplusplus
}
#endif
