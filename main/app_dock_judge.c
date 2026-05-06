/* 实现说明：这里仅做视觉到控制之间的接驳条件判定，不直接驱动硬件。 */
/*
 * app_dock_judge.c - 无人机接驳条件判定模块
 *
 * 这个文件不直接控制电机，也不直接做图像识别，而是把 app_vision.c 输出的 AprilTag 结果转成“是否允许接驳”的工程判断。
 * 它会检查：
 * - 识别到的 tag ID 是否是目标无人机；
 * - 目标是否在画面中心附近；
 * - 根据标签成像边长估算出的距离是否在安全范围内；
 * - 多帧稳定计数是否达标；
 * - 识别丢失时是否需要保留上一帧状态，避免 UI 和状态机抖动。
 *
 * 这个模块相当于视觉算法和控制状态机之间的“安全闸门”。
 */

#include "app_dock_judge.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "app_dock";

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool have_filter;                   /* EMA 滤波器是否已有历史值。 */
    uint16_t last_tag_id;               /* 上一次用于滤波的 tag ID。 */
    uint8_t ready_pass_count;           /* 连续满足 ready 条件的帧数。 */
    uint8_t ready_bad_count;            /* 连续不满足 ready 条件的帧数。 */
    uint8_t aligned_pass_count;         /* 连续满足 aligned 条件的帧数。 */
    uint8_t wrong_id_count;             /* 连续识别到错误 ID 的帧数。 */
    uint8_t invalid_hold_count;         /* 识别丢失后保留上一状态的帧数。 */
    int32_t filtered_center_x;          /* 滤波后的目标中心 x 坐标。 */
    int32_t filtered_center_y;          /* 滤波后的目标中心 y 坐标。 */
    int32_t filtered_area;              /* 滤波后的目标面积。 */
    int32_t filtered_bbox_w;            /* 滤波后的外接框宽度。 */
    int32_t filtered_bbox_h;            /* 滤波后的外接框高度。 */
    float filtered_edge_px;             /* 滤波后的 tag 边长像素估计。 */
    float filtered_angle_deg;           /* 滤波后的 tag 顶边角度。 */
    app_dock_state_t last_state;        /* 上一次输出的接驳判定状态。 */
} app_dock_judge_runtime_t;
static app_dock_judge_config_t s_cfg = {0};
static app_dock_judge_runtime_t s_rt = {0};
static bool s_inited = false;

/* -------------------------------------------------------------------------- */
/* 数学和滤波辅助函数                                                  */
/* -------------------------------------------------------------------------- */

/* 返回 int32_t 绝对值，用于偏差评分。 */
static inline int32_t app_abs_i32(int32_t v)
{
    return (v >= 0) ? v : -v;
}
/* 对 uint8_t 计数做饱和递增，避免防抖计数溢出。 */
static inline uint8_t app_sat_inc_u8(uint8_t v)
{
    return (v == UINT8_MAX) ? UINT8_MAX : (uint8_t)(v + 1U);
}
/* 对整数采样做 EMA 滤波，平滑中心点和面积。 */
static int32_t app_filter_ema_i32(int32_t prev, int32_t sample, uint8_t shift)
{
    if (shift == 0U)
    {
        return sample;
    }
    int32_t delta = sample - prev;
    return prev + ((delta >= 0) ? ((delta + (1 << (shift - 1))) >> shift)
        : -(((-delta) + (1 << (shift - 1))) >> shift));
}
/* 对浮点采样做 EMA 滤波，平滑边长和角度。 */
static float app_filter_ema_f32(float prev, float sample, uint8_t shift)
{
    if (shift == 0U)
    {
        return sample;
    }
    const float alpha = 1.0f / (float)(1U << shift);
    return prev + (sample - prev) * alpha;
}
/* 将 int32_t 数值限制在指定区间内。 */
static int32_t app_clip_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
/* 把当前视觉结果写入滤波器，目标 ID 变化时重置历史。 */
static void app_dock_apply_filter(const app_vision_result_t *vision)
{
    if (!s_rt.have_filter || (s_rt.last_tag_id != vision->tag_id))
    {
        s_rt.have_filter = true;
        s_rt.last_tag_id = vision->tag_id;
        s_rt.filtered_center_x = vision->center_x;
        s_rt.filtered_center_y = vision->center_y;
        s_rt.filtered_area = vision->area;
        s_rt.filtered_bbox_w = vision->bbox_w;
        s_rt.filtered_bbox_h = vision->bbox_h;
        s_rt.filtered_edge_px = vision->edge_px_avg;
        s_rt.filtered_angle_deg = vision->top_edge_angle_deg;
        s_rt.ready_pass_count = 0;
        s_rt.ready_bad_count = 0;
        s_rt.aligned_pass_count = 0;
        s_rt.invalid_hold_count = 0;
        return;
    }
    s_rt.filtered_center_x = app_filter_ema_i32(s_rt.filtered_center_x,
        vision->center_x,
        s_cfg.ema_shift);
    s_rt.filtered_center_y = app_filter_ema_i32(s_rt.filtered_center_y,
        vision->center_y,
        s_cfg.ema_shift);
    s_rt.filtered_area = app_filter_ema_i32(s_rt.filtered_area,
        vision->area,
        s_cfg.ema_shift);
    s_rt.filtered_bbox_w = app_filter_ema_i32(s_rt.filtered_bbox_w,
        vision->bbox_w,
        s_cfg.ema_shift);
    s_rt.filtered_bbox_h = app_filter_ema_i32(s_rt.filtered_bbox_h,
        vision->bbox_h,
        s_cfg.ema_shift);
    s_rt.filtered_edge_px = app_filter_ema_f32(s_rt.filtered_edge_px,
        vision->edge_px_avg,
        s_cfg.ema_shift);
    s_rt.filtered_angle_deg = app_filter_ema_f32(s_rt.filtered_angle_deg,
        vision->top_edge_angle_deg,
        s_cfg.ema_shift);
}
/* 根据 tag 实际尺寸、焦距和成像边长粗估距离。 */
static int32_t app_dock_estimate_distance_mm(float edge_px)
{
    if (edge_px <= 1.0f || s_cfg.focal_length_px <= 1.0f || s_cfg.tag_size_mm <= 0)
    {
        return -1;
    }
    return (int32_t)((s_cfg.focal_length_px * (float)s_cfg.tag_size_mm / edge_px) + 0.5f);
}
/* 综合中心偏差、稳定性、面积和距离计算悬停对准评分。 */
static uint8_t app_dock_calc_hover_score(const app_dock_judge_result_t *out)
{
    int32_t center_x_score = 0;
    int32_t center_y_score = 0;
    int32_t stable_score = 0;
    int32_t near_score = 0;
    int32_t dist_score = 0;
    const int32_t x_den = (s_cfg.center_x_tol > 0) ? (s_cfg.center_x_tol * 2) : 1;
    const int32_t y_den = (s_cfg.center_y_tol > 0) ? (s_cfg.center_y_tol * 2) : 1;
    center_x_score = 100 - app_clip_i32((app_abs_i32(out->dx) * 100) / x_den, 0, 100);
    center_y_score = 100 - app_clip_i32((app_abs_i32(out->dy) * 100) / y_den, 0, 100);
    if (s_cfg.min_stable_count > 0)
    {
        stable_score = app_clip_i32(((int32_t)out->stable_count * 100) / (int32_t)s_cfg.min_stable_count, 0, 100);
    }
    if (s_cfg.min_area > 0)
    {
        near_score = app_clip_i32((out->filtered_area * 100) / s_cfg.min_area, 0, 100);
    }
    if (out->est_distance_mm > 0)
    {
        if (s_cfg.use_distance_gate)
        {
            if (out->est_distance_mm >= s_cfg.min_distance_mm &&
                out->est_distance_mm <= s_cfg.max_distance_mm)
            {
                dist_score = 100;
            }
            else
            {
                const int32_t target_mid = (s_cfg.min_distance_mm + s_cfg.max_distance_mm) / 2;
                const int32_t tol = ((s_cfg.max_distance_mm - s_cfg.min_distance_mm) / 2) + 1;
                dist_score = 100 - app_clip_i32((app_abs_i32(out->est_distance_mm - target_mid) * 100) / tol, 0, 100);
            }
        }
        else
        {
            dist_score = 100;
        }
    }
    int32_t score = ((center_x_score + center_y_score) / 2) * 35
    + stable_score * 25
    + near_score * 20
    + dist_score * 20;
    score /= 100;
    if (!out->target_id_ok)
    {
        score /= 4;
    }
    if (!out->vision_valid)
    {
        score = 0;
    }
    return (uint8_t)app_clip_i32(score, 0, 100);
}
/* 填充判定结果中的基础视觉字段和滤波后字段。 */
static void app_dock_fill_result_base(const app_vision_result_t *vision,
    app_dock_judge_result_t *out)
{
    out->vision_valid = vision->valid;
    out->tag_id = vision->tag_id;
    out->frame_seq = vision->frame_seq;
    out->area = vision->area;
    out->bbox_w = vision->bbox_w;
    out->bbox_h = vision->bbox_h;
    out->stable_count = vision->stable_count;
    out->lost_count = vision->lost_count;
    out->raw_dx = vision->center_x - s_cfg.center_x_ref;
    out->raw_dy = vision->center_y - s_cfg.center_y_ref;
    out->raw_area = vision->area;
    out->raw_edge_px = vision->edge_px_avg;
    out->filtered_center_x = s_rt.filtered_center_x;
    out->filtered_center_y = s_rt.filtered_center_y;
    out->filtered_area = s_rt.filtered_area;
    out->filtered_edge_px = s_rt.filtered_edge_px;
    out->angle_deg = s_rt.filtered_angle_deg;
    out->dx = s_rt.filtered_center_x - s_cfg.center_x_ref;
    out->dy = s_rt.filtered_center_y - s_cfg.center_y_ref;
    out->ready_pass_count = s_rt.ready_pass_count;
    out->ready_bad_count = s_rt.ready_bad_count;
    out->invalid_hold_count = s_rt.invalid_hold_count;
    out->est_distance_mm = app_dock_estimate_distance_mm(s_rt.filtered_edge_px);
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

/* 填充默认接驳判定阈值，供 main 根据实际标定再覆盖。 */
void app_dock_judge_get_default_config(app_dock_judge_config_t *out)
{
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->use_target_id = true;
    out->target_tag_id = 1;
    out->center_x_ref = 160;
    out->center_y_ref = 120;
    out->center_x_tol = 28;
    out->center_y_tol = 18;
    out->min_area = 2400;
    out->min_bbox_w = 58;
    out->min_bbox_h = 72;
    out->min_stable_count = 5;
    out->use_distance_gate = true;
    out->tag_size_mm = 100;
    out->focal_length_px = 330.0f;
    out->min_distance_mm = 220;
    out->max_distance_mm = 650;
    out->ema_shift = 2;
    out->ready_enter_frames = 2;
    out->ready_exit_bad_frames = 3;
    out->aligned_enter_frames = 1;
    out->wrong_id_enter_frames = 2;
    out->lost_hold_frames = 3;
}
/* 保存配置并初始化内部滤波、防抖和状态机。 */
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg)
{
    if (cfg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    app_dock_judge_reset();
    s_inited = true;
    ESP_LOGI(TAG,
        "dock init: target_en=%d target_id=%u ref=(%ld,%ld) tol=(%ld,%ld) area>=%ld bbox>=(%ld,%ld) stable>=%u tag=%ldmm focal=%.1fpx dist_gate=%d dist=[%ld,%ld]",
        s_cfg.use_target_id,
        (unsigned)s_cfg.target_tag_id,
        (long)s_cfg.center_x_ref,
        (long)s_cfg.center_y_ref,
        (long)s_cfg.center_x_tol,
        (long)s_cfg.center_y_tol,
        (long)s_cfg.min_area,
        (long)s_cfg.min_bbox_w,
        (long)s_cfg.min_bbox_h,
        (unsigned)s_cfg.min_stable_count,
        (long)s_cfg.tag_size_mm,
        (double)s_cfg.focal_length_px,
        s_cfg.use_distance_gate,
        (long)s_cfg.min_distance_mm,
        (long)s_cfg.max_distance_mm);
    return ESP_OK;
}
/* 切换目标 tag ID，并清空旧目标留下的滤波和计数状态。 */
esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter)
{
    if (!s_inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg.target_tag_id = target_tag_id;
    s_cfg.use_target_id = enable_filter;
    s_rt.have_filter = false;
    s_rt.last_tag_id = 0;
    s_rt.ready_pass_count = 0;
    s_rt.ready_bad_count = 0;
    s_rt.aligned_pass_count = 0;
    s_rt.wrong_id_count = 0;
    s_rt.invalid_hold_count = 0;
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;
    ESP_LOGI(TAG, "dock target updated: enable=%d target_id=%u", s_cfg.use_target_id, (unsigned)s_cfg.target_tag_id);
    return ESP_OK;
}
/* 重置运行态，使下一帧从搜索状态重新开始判断。 */
void app_dock_judge_reset(void)
{
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;
}
/* 主判定入口：把单帧视觉结果转换成接驳状态、评分和各项门限结果。 */
bool app_dock_judge_process(const app_vision_result_t *vision,
    app_dock_judge_result_t *out)
{
    if (!s_inited || vision == NULL || out == NULL)
    {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!vision->valid)
    {
        if (s_rt.invalid_hold_count < s_cfg.lost_hold_frames)
        {
            s_rt.invalid_hold_count++;
        }
        s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);
        s_rt.ready_pass_count = 0;
        s_rt.aligned_pass_count = 0;
        s_rt.wrong_id_count = 0;
        out->vision_valid = false;
        out->frame_seq = vision->frame_seq;
        out->lost_count = vision->lost_count;
        out->invalid_hold_count = s_rt.invalid_hold_count;
        out->ready_bad_count = s_rt.ready_bad_count;
        out->state = APP_DOCK_STATE_SEARCHING;
        if (s_rt.last_state != APP_DOCK_STATE_SEARCHING &&
            s_rt.invalid_hold_count < s_cfg.lost_hold_frames)
        {
            out->state = s_rt.last_state;
            out->filtered_center_x = s_rt.filtered_center_x;
            out->filtered_center_y = s_rt.filtered_center_y;
            out->filtered_area = s_rt.filtered_area;
            out->filtered_edge_px = s_rt.filtered_edge_px;
            out->angle_deg = s_rt.filtered_angle_deg;
            out->dx = s_rt.filtered_center_x - s_cfg.center_x_ref;
            out->dy = s_rt.filtered_center_y - s_cfg.center_y_ref;
            out->est_distance_mm = app_dock_estimate_distance_mm(s_rt.filtered_edge_px);
            out->hover_score = 0;
            return true;
        }
        s_rt.have_filter = false;
        s_rt.last_tag_id = 0;
        s_rt.last_state = APP_DOCK_STATE_SEARCHING;
        return true;
    }
    s_rt.invalid_hold_count = 0;
    app_dock_apply_filter(vision);
    app_dock_fill_result_base(vision, out);
    out->target_id_ok = (!s_cfg.use_target_id) || (vision->tag_id == s_cfg.target_tag_id);
    out->centered_ok = (app_abs_i32(out->dx) <= s_cfg.center_x_tol) &&
    (app_abs_i32(out->dy) <= s_cfg.center_y_tol);
    out->near_ok = (s_rt.filtered_area >= s_cfg.min_area) &&
    (s_rt.filtered_bbox_w >= s_cfg.min_bbox_w) &&
    (s_rt.filtered_bbox_h >= s_cfg.min_bbox_h);
    out->stable_ok = (vision->stable_count >= s_cfg.min_stable_count);
    if (out->est_distance_mm > 0)
    {
        if (s_cfg.use_distance_gate)
        {
            out->distance_ok = (out->est_distance_mm >= s_cfg.min_distance_mm) &&
            (out->est_distance_mm <= s_cfg.max_distance_mm);
        }
        else
        {
            out->distance_ok = true;
        }
    }
    else
    {
        out->distance_ok = false;
    }
    if (!out->target_id_ok)
    {
        s_rt.wrong_id_count = app_sat_inc_u8(s_rt.wrong_id_count);
        s_rt.ready_pass_count = 0;
        s_rt.ready_bad_count = 0;
        s_rt.aligned_pass_count = 0;
        if (s_rt.wrong_id_count >= s_cfg.wrong_id_enter_frames)
        {
            s_rt.last_state = APP_DOCK_STATE_WRONG_ID;
        }
        else
        {
            s_rt.last_state = APP_DOCK_STATE_TRACKING;
        }
        out->state = s_rt.last_state;
        out->hover_score = app_dock_calc_hover_score(out);
        out->ready_pass_count = s_rt.ready_pass_count;
        out->ready_bad_count = s_rt.ready_bad_count;
        out->invalid_hold_count = s_rt.invalid_hold_count;
        return true;
    }
    s_rt.wrong_id_count = 0;
    const bool ready_cond = out->centered_ok &&
    out->near_ok &&
    out->stable_ok &&
    (!s_cfg.use_distance_gate || out->distance_ok);
    const bool aligned_cond = out->centered_ok &&
    (out->near_ok || out->stable_ok);
    if (ready_cond)
    {
        s_rt.ready_pass_count = app_sat_inc_u8(s_rt.ready_pass_count);
        s_rt.ready_bad_count = 0;
    }
    else
    {
        s_rt.ready_pass_count = 0;
        s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);
    }
    if (aligned_cond)
    {
        s_rt.aligned_pass_count = app_sat_inc_u8(s_rt.aligned_pass_count);
    }
    else
    {
        s_rt.aligned_pass_count = 0;
    }
    switch (s_rt.last_state) {
    case APP_DOCK_STATE_READY_TO_DOCK:
        if (ready_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames))
        {
            s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
        }
        else if (aligned_cond)
        {
            s_rt.last_state = APP_DOCK_STATE_ALIGNED;
        }
        else
        {
            s_rt.last_state = APP_DOCK_STATE_TRACKING;
        }
        break;
    case APP_DOCK_STATE_ALIGNED:
        if (ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames))
        {
            s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
        }
        else if (aligned_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames))
        {
            s_rt.last_state = APP_DOCK_STATE_ALIGNED;
        }
        else
        {
            s_rt.last_state = APP_DOCK_STATE_TRACKING;
        }
        break;
    case APP_DOCK_STATE_TRACKING:
    case APP_DOCK_STATE_SEARCHING:
    case APP_DOCK_STATE_WRONG_ID:
    default:
        if (ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames))
        {
            s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
        }
        else if (aligned_cond && (s_rt.aligned_pass_count >= s_cfg.aligned_enter_frames))
        {
            s_rt.last_state = APP_DOCK_STATE_ALIGNED;
        }
        else
        {
            s_rt.last_state = APP_DOCK_STATE_TRACKING;
        }
        break;
    }
    out->state = s_rt.last_state;
    out->hover_score = app_dock_calc_hover_score(out);
    out->ready_pass_count = s_rt.ready_pass_count;
    out->ready_bad_count = s_rt.ready_bad_count;
    out->invalid_hold_count = s_rt.invalid_hold_count;
    return true;
}

/* -------------------------------------------------------------------------- */
/* 格式化辅助函数                                                          */
/* -------------------------------------------------------------------------- */

/* 将接驳状态枚举转为日志和云端快照使用的短文本。 */
const char *app_dock_judge_state_to_text(app_dock_state_t state)
{
    switch (state) {
    case APP_DOCK_STATE_SEARCHING:
        return "searching";
    case APP_DOCK_STATE_WRONG_ID:
        return "wrong_id";
    case APP_DOCK_STATE_TRACKING:
        return "tracking";
    case APP_DOCK_STATE_ALIGNED:
        return "aligned";
    case APP_DOCK_STATE_READY_TO_DOCK:
        return "ready";
    default:
        return "unknown";
    }
}
/* 格式化一行适合 UI 状态栏显示的接驳状态。 */
void app_dock_judge_format_status(const app_dock_judge_result_t *result,
    char *buf,
    size_t buf_len)
{
    if (result == NULL || buf == NULL || buf_len == 0)
    {
        return;
    }
    switch (result->state) {
    case APP_DOCK_STATE_SEARCHING:
        snprintf(buf, buf_len, "dock: searching target");
        break;
    case APP_DOCK_STATE_WRONG_ID:
        snprintf(buf, buf_len, "dock: wrong tag id");
        break;
    case APP_DOCK_STATE_TRACKING:
        snprintf(buf, buf_len, "dock: target tracking");
        break;
    case APP_DOCK_STATE_ALIGNED:
        snprintf(buf, buf_len, "dock: aligned / hold");
        break;
    case APP_DOCK_STATE_READY_TO_DOCK:
        snprintf(buf, buf_len, "dock: ready to dock");
        break;
    default:
        snprintf(buf, buf_len, "dock: unknown");
        break;
    }
}
/* 格式化详细调试文本，方便观察偏差、距离、评分和门限命中情况。 */
void app_dock_judge_format_detail(const app_dock_judge_result_t *result,
    char *buf,
    size_t buf_len)
{
    if (result == NULL || buf == NULL || buf_len == 0)
    {
        return;
    }
    if (!result->vision_valid)
    {
        if (result->state != APP_DOCK_STATE_SEARCHING)
        {
            snprintf(buf,
                buf_len,
                "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ld e:%.1f",
                (unsigned)result->invalid_hold_count,
                (unsigned)result->lost_count,
                (long)result->dx,
                (long)result->dy,
                (long)result->est_distance_mm,
                (double)result->filtered_edge_px);
        }
        else
        {
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        }
        return;
    }
    snprintf(buf,
        buf_len,
        "dock dbg: id:%u c:%ld,%ld b:%ldx%ld dx:%ld dy:%ld z:%ldmm e:%.1f/%.1f ang:%d st:%u score:%u f:%c%c%c%c%c r:%u",
        (unsigned)result->tag_id,
        (long)result->filtered_center_x,
        (long)result->filtered_center_y,
        (long)result->bbox_w,
        (long)result->bbox_h,
        (long)result->dx,
        (long)result->dy,
        (long)result->est_distance_mm,
        (double)result->raw_edge_px,
        (double)result->filtered_edge_px,
        (int)result->angle_deg,
        (unsigned)result->stable_count,
        (unsigned)result->hover_score,
        result->target_id_ok ? 'I' : 'x',
        result->centered_ok ? 'C' : 'x',
        result->near_ok ? 'N' : 'x',
        result->stable_ok ? 'S' : 'x',
        result->distance_ok ? 'D' : 'x',
        (unsigned)result->ready_pass_count);
}
