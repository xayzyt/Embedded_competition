#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// AprilTag 检测结果，包含 ID、角点、包围盒和姿态近似信息。
typedef struct {
    bool valid;                 // 是否检测成功。
    uint16_t id;                // 解码得到的 tag36h11 ID。
    uint8_t hamming;            // 与码表最小汉明距离。
    uint8_t rotation;           // 解码时采用的旋转方向。
    uint8_t threshold;          // 本帧二值化阈值。
    uint8_t border_dark_pct;    // 外边框暗像素占比。
    int32_t center_x;           // 中心 X。
    int32_t center_y;           // 中心 Y。
    int32_t area;               // 候选连通域面积。
    int32_t bbox_x;             // 包围盒左上 X。
    int32_t bbox_y;             // 包围盒左上 Y。
    int32_t bbox_w;             // 包围盒宽。
    int32_t bbox_h;             // 包围盒高。
    int32_t corner_tl_x;        // 左上角 X。
    int32_t corner_tl_y;        // 左上角 Y。
    int32_t corner_tr_x;        // 右上角 X。
    int32_t corner_tr_y;        // 右上角 Y。
    int32_t corner_br_x;        // 右下角 X。
    int32_t corner_br_y;        // 右下角 Y。
    int32_t corner_bl_x;        // 左下角 X。
    int32_t corner_bl_y;        // 左下角 Y。
    float edge_px_avg;          // 四边平均边长。
    float top_edge_angle_deg;   // 上边缘角度。
} app_apriltag_result_t;
// 初始化检测器内部缓存。
esp_err_t app_apriltag_init(void);
// 在灰度图上检测 tag36h11，输出最优标签。
bool app_apriltag_detect_tag36h11(const uint8_t *gray,
                                  uint32_t width,
                                  uint32_t height,
                                  app_apriltag_result_t *out);
#ifdef __cplusplus
}
#endif
