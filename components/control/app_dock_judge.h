/*
 * 无人机接驳条件判定模块公开接口。
 * 将 AprilTag 视觉检测结果转化为"是否允许接驳"的工程判定。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "app_dock_types.h"
#include "app_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 接驳判定配置参数，main 可根据实际标定覆盖默认值。 */
typedef struct {
    bool use_target_id;              /* 是否启用目标 tag ID 过滤。 */
    uint16_t target_tag_id;          /* 期望的 tag ID。 */

    int32_t center_x_ref;            /* 画面中心参考 x 坐标。 */
    int32_t center_y_ref;            /* 画面中心参考 y 坐标。 */
    int32_t center_x_tol;            /* 中心 x 容差（像素）。 */
    int32_t center_y_tol;            /* 中心 y 容差（像素）。 */

    int32_t min_area;                /* 最小连通域面积。 */
    int32_t min_bbox_w;              /* 最小外接框宽度。 */
    int32_t min_bbox_h;              /* 最小外接框高度。 */
    uint16_t min_stable_count;       /* 最小稳定帧数。 */

    bool use_distance_gate;          /* 是否启用距离门控。 */
    int32_t tag_size_mm;             /* tag 物理尺寸（毫米）。 */
    float focal_length_px;           /* 焦距（像素）。 */
    int32_t min_distance_mm;         /* 最小允许距离（毫米）。 */
    int32_t max_distance_mm;         /* 最大允许距离（毫米）。 */

    uint8_t ema_shift;               /* EMA 滤波平滑指数（2^shift 为窗口大小）。 */
    uint8_t ready_enter_frames;      /* 进入 ready 状态所需连续帧数。 */
    uint8_t ready_exit_bad_frames;   /* 退出 ready 状态所需连续坏帧数。 */
    uint8_t aligned_enter_frames;    /* 进入 aligned 状态所需连续帧数。 */
    uint8_t wrong_id_enter_frames;   /* 进入 wrong_id 状态所需连续帧数。 */
    uint8_t lost_hold_frames;        /* 丢失后保持上一状态的帧数。 */
} app_dock_judge_config_t;

/* 用预设的工程默认值填充配置。 */
void app_dock_judge_get_default_config(app_dock_judge_config_t *out);

/* 保存配置并初始化滤波和状态机。 */
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg);

/* 切换目标 tag ID，同时清空旧的滤波和计数状态。 */
esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter);

/* 重置内部运行态。 */
void app_dock_judge_reset(void);

/* 主判定入口：把单帧视觉结果转换为接驳状态和各项门限结果。 */
bool app_dock_judge_process(const app_vision_result_t *vision,
                            app_dock_judge_result_t *out);

/* 状态枚举转短文本。 */
const char *app_dock_judge_state_to_text(app_dock_state_t state);

/* 格式化一行适合 UI 状态栏显示的接驳状态文本。 */
void app_dock_judge_format_status(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);

/* 格式化详细调试文本，包含偏差、距离、评分和门限命中情况。 */
void app_dock_judge_format_detail(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len);

#ifdef __cplusplus
}
#endif
