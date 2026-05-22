/*
 * 接驳相关公共类型定义。
 * 供 app_dock_judge、app_ctrl、app_ui 等模块共享引用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 接驳判定状态机状态枚举。 */
typedef enum {
    APP_DOCK_STATE_SEARCHING = 0,  /* 正在搜索目标 tag。 */
    APP_DOCK_STATE_WRONG_ID,       /* 识别到非目标 tag ID。 */
    APP_DOCK_STATE_TRACKING,       /* 正在跟踪目标但尚未对齐。 */
    APP_DOCK_STATE_ALIGNED,        /* 目标已对齐中心，待稳定。 */
    APP_DOCK_STATE_READY_TO_DOCK,  /* 目标稳定对齐，可执行接驳。 */
} app_dock_state_t;

/* 接驳判定结果，包含原始视觉数据、滤波数据和各项门限检查结果。 */
typedef struct {
    app_dock_state_t state;        /* 当前接驳判定状态。 */
    bool vision_valid;             /* 视觉检测是否有效。 */
    bool target_id_ok;             /* tag ID 是否匹配目标。 */
    bool centered_ok;              /* 目标是否在画面中心容差范围内。 */
    bool near_ok;                  /* 目标面积/边框是否满足近距离条件。 */
    bool stable_ok;                /* 连续稳定检测帧数是否达标。 */
    bool distance_ok;              /* 估算距离是否在允许范围内。 */

    uint16_t tag_id;               /* 当前识别到的 tag ID。 */
    int32_t dx;                    /* 滤波后中心 x 偏差（像素）。 */
    int32_t dy;                    /* 滤波后中心 y 偏差（像素）。 */
    int32_t area;                  /* tag 连通域面积（像素平方）。 */
    int32_t bbox_w;                /* 外接框宽度。 */
    int32_t bbox_h;                /* 外接框高度。 */
    uint16_t stable_count;         /* 连续稳定检测帧数。 */
    uint16_t lost_count;           /* 连续丢失/无效帧数。 */
    uint32_t frame_seq;            /* 帧序号。 */

    int32_t raw_dx;                /* 未滤波的中心 x 偏差。 */
    int32_t raw_dy;                /* 未滤波的中心 y 偏差。 */
    int32_t raw_area;              /* 未滤波的连通域面积。 */
    int32_t filtered_center_x;     /* 滤波后目标中心 x 坐标。 */
    int32_t filtered_center_y;     /* 滤波后目标中心 y 坐标。 */
    int32_t filtered_area;         /* 滤波后目标面积。 */

    float raw_edge_px;             /* 未滤波的 tag 边长像素估计。 */
    float filtered_edge_px;        /* 滤波后 tag 边长像素估计。 */
    float angle_deg;               /* tag 顶边角度（度）。 */
    int32_t est_distance_mm;       /* 根据 tag 边长估算的距离（毫米）。 */
    uint8_t hover_score;           /* 综合对准悬停评分（0-100）。 */

    uint8_t ready_pass_count;      /* 连续满足 ready 条件的帧数。 */
    uint8_t ready_bad_count;       /* 连续不满足 ready 条件的帧数。 */
    uint8_t invalid_hold_count;    /* 识别丢失后保留上一状态的帧数。 */
} app_dock_judge_result_t;
