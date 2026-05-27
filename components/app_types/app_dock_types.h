#pragma once
#include <stdbool.h>
#include <stdint.h>
// 对接判定状态，描述从搜索标签到允许进入对接的完整阶段。
typedef enum {
    APP_DOCK_STATE_SEARCHING = 0,
    APP_DOCK_STATE_WRONG_ID,
    APP_DOCK_STATE_TRACKING,
    APP_DOCK_STATE_ALIGNED,
    APP_DOCK_STATE_READY_TO_DOCK,
} app_dock_state_t;
// 对接判定输出快照，供控制器、UI 和云端状态上报共同使用。
typedef struct {
    app_dock_state_t state;      // 当前对接状态。
    bool vision_valid;           // 本帧视觉结果是否有效。
    bool target_id_ok;           // 标签 ID 是否匹配目标。
    bool centered_ok;            // 标签中心是否进入允许误差范围。
    bool near_ok;                // 标签面积/尺寸是否达到靠近阈值。
    bool stable_ok;              // 连续稳定帧数是否达标。
    bool distance_ok;            // 距离门控是否通过。
    uint16_t tag_id;             // 当前检测到的 AprilTag ID。
    int32_t dx;                  // 滤波后中心相对参考点的 X 偏差。
    int32_t dy;                  // 滤波后中心相对参考点的 Y 偏差。
    int32_t area;                // 滤波后的标签面积。
    int32_t bbox_w;              // 标签包围盒宽度。
    int32_t bbox_h;              // 标签包围盒高度。
    uint16_t stable_count;       // 连续稳定命中帧数。
    uint16_t lost_count;         // 连续丢失帧数。
    uint32_t frame_seq;          // 视觉帧序号。
    int32_t raw_dx;              // 未滤波 X 偏差。
    int32_t raw_dy;              // 未滤波 Y 偏差。
    int32_t raw_area;            // 未滤波面积。
    int32_t filtered_center_x;   // 滤波后的中心 X。
    int32_t filtered_center_y;   // 滤波后的中心 Y。
    int32_t filtered_area;       // 滤波后的面积。
    float raw_edge_px;           // 未滤波平均边长像素。
    float filtered_edge_px;      // 滤波后的平均边长像素。
    float angle_deg;             // 标签上边缘角度，用于观察姿态。
    int32_t est_distance_mm;     // 根据边长估算的距离，单位 mm。
    uint8_t hover_score;         // 悬停稳定评分。
    uint8_t ready_pass_count;    // 连续进入 ready 条件的帧数。
    uint8_t ready_bad_count;     // ready 状态下连续不达标帧数。
    uint8_t invalid_hold_count;  // 视觉短时丢失时保持旧结果的帧数。
} app_dock_judge_result_t;
