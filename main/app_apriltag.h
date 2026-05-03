#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;                 /* 是否成功识别到合法 Tag36h11。 */
    uint16_t id;                /* 识别出的 tag ID。 */
    uint8_t hamming;            /* 与码表最接近码字的汉明距离。 */
    uint8_t rotation;           /* 解码时匹配到的旋转方向。 */
    uint8_t threshold;          /* 本次候选区域使用的灰度阈值。 */
    uint8_t border_dark_pct;    /* 外圈黑边判定为黑色的比例。 */
    int32_t center_x;           /* tag 中心点 x 坐标。 */
    int32_t center_y;           /* tag 中心点 y 坐标。 */
    int32_t area;               /* 候选连通域面积。 */
    int32_t bbox_x;             /* 外接框左上角 x 坐标。 */
    int32_t bbox_y;             /* 外接框左上角 y 坐标。 */
    int32_t bbox_w;             /* 外接框宽度。 */
    int32_t bbox_h;             /* 外接框高度。 */

    /* 四角点（用于后续 HUD 升级 / 位姿估算） */
    int32_t corner_tl_x;        /* 左上角 x 坐标。 */
    int32_t corner_tl_y;        /* 左上角 y 坐标。 */
    int32_t corner_tr_x;        /* 右上角 x 坐标。 */
    int32_t corner_tr_y;        /* 右上角 y 坐标。 */
    int32_t corner_br_x;        /* 右下角 x 坐标。 */
    int32_t corner_br_y;        /* 右下角 y 坐标。 */
    int32_t corner_bl_x;        /* 左下角 x 坐标。 */
    int32_t corner_bl_y;        /* 左下角 y 坐标。 */

    /* 边长与朝向（粗距离估算的输入） */
    float edge_px_avg;          /* 四条边的平均像素长度。 */
    float top_edge_angle_deg;   /* 顶边相对水平线的角度，单位为度。 */
} app_apriltag_result_t;

esp_err_t app_apriltag_init(void);
bool app_apriltag_detect_tag36h11(const uint8_t *gray,
                                  uint32_t width,
                                  uint32_t height,
                                  app_apriltag_result_t *out);

#ifdef __cplusplus
}
#endif
