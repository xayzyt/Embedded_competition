#pragma once

/*
 * 将原始 AprilTag 视觉结果转换为接驳可用性判定。
 * 控制层只消费本模块输出，避免直接依赖视觉细节。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "app_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_DOCK_STATE_SEARCHING = 0,
    APP_DOCK_STATE_WRONG_ID,
    APP_DOCK_STATE_TRACKING,
    APP_DOCK_STATE_ALIGNED,
    APP_DOCK_STATE_READY_TO_DOCK,
} app_dock_state_t;

typedef struct {
    bool use_target_id;             /* 是否启用目标 tag ID 过滤。 */
    uint16_t target_tag_id;         /* 允许接驳的目标 tag ID。 */

    int32_t center_x_ref;           /* 期望中心点 x 坐标。 */
    int32_t center_y_ref;           /* 期望中心点 y 坐标。 */
    int32_t center_x_tol;           /* x 方向允许偏差。 */
    int32_t center_y_tol;           /* y 方向允许偏差。 */

    int32_t min_area;               /* 允许接驳所需的最小识别面积。 */
    int32_t min_bbox_w;             /* 允许接驳所需的最小外接框宽度。 */
    int32_t min_bbox_h;             /* 允许接驳所需的最小外接框高度。 */
    uint16_t min_stable_count;      /* 允许接驳所需的最小稳定帧数。 */

    /* 100 mm AprilTag 的粗距离估算参数（后续可标定） */
    bool use_distance_gate;         /* 是否启用距离范围门限。 */
    int32_t tag_size_mm;            /* 实际 tag 边长，单位为毫米。 */
    float focal_length_px;          /* 用于粗距离估算的等效焦距。 */
    int32_t min_distance_mm;        /* 允许接驳的最近距离。 */
    int32_t max_distance_mm;        /* 允许接驳的最远距离。 */

    /* 平滑与防抖参数 */
    uint8_t ema_shift;              /* EMA 滤波右移系数，2 表示 1/4 新值权重。 */
    uint8_t ready_enter_frames;     /* 连续满足 ready 条件多少帧才进入 ready。 */
    uint8_t ready_exit_bad_frames;  /* 连续多少帧不满足 ready 才退出 ready。 */
    uint8_t aligned_enter_frames;   /* 连续多少帧满足对准条件才进入 aligned。 */
    uint8_t wrong_id_enter_frames;  /* 连续多少帧错误 ID 才显示 wrong_id。 */
    uint8_t lost_hold_frames;       /* 丢 1~N 帧时保留上一个稳定状态。 */
} app_dock_judge_config_t;

typedef struct {
    app_dock_state_t state;         /* 当前接驳判定状态。 */
    bool vision_valid;              /* 当前视觉结果是否有效。 */
    bool target_id_ok;              /* 识别 ID 是否匹配目标 ID。 */
    bool centered_ok;               /* 目标中心是否在允许偏差内。 */
    bool near_ok;                   /* 目标面积和尺寸是否达到靠近要求。 */
    bool stable_ok;                 /* 稳定帧数是否达到要求。 */
    bool distance_ok;               /* 粗估距离是否在允许范围内。 */

    uint16_t tag_id;                /* 当前识别到的 tag ID。 */
    int32_t dx;                     /* 滤波后目标中心相对参考点的 x 偏差。 */
    int32_t dy;                     /* 滤波后目标中心相对参考点的 y 偏差。 */
    int32_t area;                   /* 滤波后的目标面积。 */
    int32_t bbox_w;                 /* 滤波后的外接框宽度。 */
    int32_t bbox_h;                 /* 滤波后的外接框高度。 */
    uint16_t stable_count;          /* 视觉模块给出的稳定计数。 */
    uint16_t lost_count;            /* 视觉模块给出的丢失计数。 */
    uint32_t frame_seq;             /* 视觉结果对应的帧序号。 */

    int32_t raw_dx;                 /* 未滤波的 x 偏差。 */
    int32_t raw_dy;                 /* 未滤波的 y 偏差。 */
    int32_t raw_area;               /* 未滤波的目标面积。 */
    int32_t filtered_center_x;      /* 滤波后的目标中心 x 坐标。 */
    int32_t filtered_center_y;      /* 滤波后的目标中心 y 坐标。 */
    int32_t filtered_area;          /* 滤波后的目标面积副本，用于 UI 展示。 */

    float raw_edge_px;              /* 未滤波的 tag 平均边长像素。 */
    float filtered_edge_px;         /* 滤波后的 tag 平均边长像素。 */
    float angle_deg;                /* tag 顶边角度，单位为度。 */
    int32_t est_distance_mm;        /* 根据 tag 尺寸估算的粗距离。 */
    uint8_t hover_score;            /* 综合对准程度评分。 */

    uint8_t ready_pass_count;       /* 连续满足 ready 条件的帧数。 */
    uint8_t ready_bad_count;        /* 连续不满足 ready 条件的帧数。 */
    uint8_t invalid_hold_count;     /* 视觉无效时保持上一状态的帧数。 */
} app_dock_judge_result_t;

/* 填充接驳判定模块的默认阈值配置。 */
void app_dock_judge_get_default_config(app_dock_judge_config_t *out);

/* 使用指定配置初始化接驳判定模块。 */
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg);

/* 更新目标 tag ID，并决定是否启用 ID 过滤。 */
esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter);

/* 清空滤波器和防抖计数，回到搜索状态。 */
void app_dock_judge_reset(void);

/* 每帧输入视觉结果，输出当前接驳判定结果。 */
bool app_dock_judge_process(const app_vision_result_t *vision,
                            app_dock_judge_result_t *out);

/* 将接驳枚举状态转换成英文短文本。 */
const char *app_dock_judge_state_to_text(app_dock_state_t state);

/* 格式化面向 UI 状态栏的简短接驳状态。 */
void app_dock_judge_format_status(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);

/* 格式化包含偏差、距离、评分和判定 flags 的调试详情。 */
void app_dock_judge_format_detail(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);

#ifdef __cplusplus
}
#endif
