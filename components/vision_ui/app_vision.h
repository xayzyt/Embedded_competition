#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
// 视觉管线接口：相机提交 RGB565 帧，后台输出 AprilTag 检测结果和统计。

#ifdef __cplusplus
extern "C" {
#endif
// 视觉模块输出结果，融合 AprilTag 信息和原图/灰度图映射关系。
typedef struct {
    bool valid;                 // 是否检测到有效标签。
    uint16_t tag_id;            // 标签 ID。
    uint8_t hamming;            // 解码汉明距离。
    uint8_t rotation;           // 解码旋转方向。
    uint8_t threshold;          // 二值化阈值。
    uint8_t border_dark_pct;    // 外边框暗像素占比。
    int32_t center_x;           // 原图坐标系中心 X。
    int32_t center_y;           // 原图坐标系中心 Y。
    int32_t area;               // 原图映射后的面积近似。
    int32_t bbox_x;             // 原图坐标系包围盒 X。
    int32_t bbox_y;             // 原图坐标系包围盒 Y。
    int32_t bbox_w;             // 原图坐标系包围盒宽。
    int32_t bbox_h;             // 原图坐标系包围盒高。
    int32_t corner_tl_x;        // 左上角 X。
    int32_t corner_tl_y;        // 左上角 Y。
    int32_t corner_tr_x;        // 右上角 X。
    int32_t corner_tr_y;        // 右上角 Y。
    int32_t corner_br_x;        // 右下角 X。
    int32_t corner_br_y;        // 右下角 Y。
    int32_t corner_bl_x;        // 左下角 X。
    int32_t corner_bl_y;        // 左下角 Y。
    float edge_px_avg;          // 原图尺度下的平均边长。
    float top_edge_angle_deg;   // 上边缘角度。
    uint32_t src_width;         // 输入帧宽。
    uint32_t src_height;        // 输入帧高。
    uint32_t crop_x;            // 灰度检测区域对应的裁剪 X。
    uint32_t crop_y;            // 灰度检测区域对应的裁剪 Y。
    uint32_t crop_w;            // 裁剪宽。
    uint32_t crop_h;            // 裁剪高。
    uint32_t gray_width;        // 灰度图宽。
    uint32_t gray_height;       // 灰度图高。
    uint32_t frame_seq;         // 帧序号。
    uint32_t detect_ms;         // 检测耗时。
    uint16_t stable_count;      // 连续检测成功帧数。
    uint16_t lost_count;        // 连续丢失帧数。
} app_vision_result_t;
// 原始帧元信息。
typedef struct {
    uint32_t width;
    uint32_t height;
    size_t len;
    uint32_t seq;
    uint32_t tick_ms;
} app_vision_frame_info_t;
// 灰度检测帧元信息，保留原图到灰度图的映射参数。
typedef struct {
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
// 初始化、启动和提交 RGB565 帧。
esp_err_t app_vision_init(void);
esp_err_t app_vision_start(void);
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
                                  uint32_t width,
                                  uint32_t height,
                                  size_t len);
// 查询最近一次检测结果。
bool app_vision_get_latest_result(app_vision_result_t *out);
#ifdef __cplusplus
}
#endif
