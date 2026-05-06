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
    bool valid;                    /* 当前检测结果是否有效。 */
    uint16_t tag_id;               /* 识别到的 AprilTag ID。 */
    uint8_t hamming;               /* 汉明距离，越小表示识别结果越可信。 */
    uint8_t rotation;              /* tag 旋转角编码（0/1/2/3）。 */
    uint8_t threshold;             /* 检测器使用的二值化阈值。 */
    uint8_t border_dark_pct;       /* tag 黑色边框占比百分比。 */
    int32_t center_x;              /* tag 中心点 x 坐标（像素）。 */
    int32_t center_y;              /* tag 中心点 y 坐标（像素）。 */
    int32_t area;                  /* tag 四边形面积（像素平方）。 */
    int32_t bbox_x;                /* 外接框左上角 x。 */
    int32_t bbox_y;                /* 外接框左上角 y。 */
    int32_t bbox_w;                /* 外接框宽度。 */
    int32_t bbox_h;                /* 外接框高度。 */

    int32_t corner_tl_x;           /* 左上角点 x。 */
    int32_t corner_tl_y;           /* 左上角点 y。 */
    int32_t corner_tr_x;           /* 右上角点 x。 */
    int32_t corner_tr_y;           /* 右上角点 y。 */
    int32_t corner_br_x;           /* 右下角点 x。 */
    int32_t corner_br_y;           /* 右下角点 y。 */
    int32_t corner_bl_x;           /* 左下角点 x。 */
    int32_t corner_bl_y;           /* 左下角点 y。 */

    float edge_px_avg;             /* tag 四条边平均像素长度。 */
    float top_edge_angle_deg;      /* tag 顶边相对水平线的角度（度）。 */

    uint32_t src_width;            /* 原始帧宽度。 */
    uint32_t src_height;           /* 原始帧高度。 */
    uint32_t crop_x;               /* 灰度裁剪区左上角 x。 */
    uint32_t crop_y;               /* 灰度裁剪区左上角 y。 */
    uint32_t crop_w;               /* 灰度裁剪区宽度。 */
    uint32_t crop_h;               /* 灰度裁剪区高度。 */
    uint32_t gray_width;           /* 内部灰度图宽度。 */
    uint32_t gray_height;          /* 内部灰度图高度。 */

    uint32_t frame_seq;            /* 帧序号。 */
    uint32_t detect_ms;            /* 检测耗时（毫秒）。 */
    uint16_t stable_count;         /* 连续稳定检测帧数。 */
    uint16_t lost_count;           /* 连续丢失/无效帧数。 */
} app_vision_result_t;

typedef struct {
    uint32_t width;                /* 帧宽度（像素）。 */
    uint32_t height;               /* 帧高度（像素）。 */
    size_t len;                    /* 帧数据字节数。 */
    uint32_t seq;                  /* 帧序号。 */
    uint32_t tick_ms;              /* 帧到达时的系统毫秒时间戳。 */
} app_vision_frame_info_t;

typedef struct {
    uint32_t src_width;            /* 原始帧宽度。 */
    uint32_t src_height;           /* 原始帧高度。 */
    uint32_t gray_width;           /* 灰度图宽度。 */
    uint32_t gray_height;          /* 灰度图高度。 */
    uint32_t crop_x;               /* 裁剪区左上角 x。 */
    uint32_t crop_y;               /* 裁剪区左上角 y。 */
    uint32_t crop_w;               /* 裁剪区宽度。 */
    uint32_t crop_h;               /* 裁剪区高度。 */
    size_t gray_len;               /* 灰度图数据字节数。 */
    uint32_t seq;                  /* 帧序号。 */
    uint32_t tick_ms;              /* 帧到达时的系统毫秒时间戳。 */
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
