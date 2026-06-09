#include "app_dock_judge.h"
#include <stdio.h>
#include <string.h>

// 对接判定器：对 AprilTag 结果做目标 ID、居中、靠近、稳定和距离门控，
// 并用 EMA 与滞回减少抖动导致的误触发。

typedef struct {
    bool have_filter;
    uint16_t last_tag_id;
    uint8_t ready_pass_count;
    uint8_t ready_bad_count;
    uint8_t aligned_pass_count;
    uint8_t wrong_id_count;
    uint8_t invalid_hold_count;
    int32_t filtered_center_x;
    int32_t filtered_center_y;
    int32_t filtered_area;
    int32_t filtered_bbox_w;
    int32_t filtered_bbox_h;
    float filtered_edge_px;
    float filtered_angle_deg;
    bool have_processed_frame;
    uint32_t last_processed_frame_seq;
    app_dock_state_t last_state;
} app_dock_judge_runtime_t;
static app_dock_judge_config_t s_cfg = {0};
static app_dock_judge_runtime_t s_rt = {0};
static bool s_inited = false;
static inline int32_t app_abs_i32(int32_t v)
{
    return (v >= 0) ? v : -v;
}
static inline uint8_t app_sat_inc_u8(uint8_t v)
{
    return (v == UINT8_MAX) ? UINT8_MAX : (uint8_t)(v + 1U);
}
// 整数 EMA，使用 shift 表示 1/(2^shift) 的滤波系数。
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
static float app_filter_ema_f32(float prev, float sample, uint8_t shift)
{
    if (shift == 0U)
    {
        return sample;
    }
    const float alpha = 1.0f / (float)(1U << shift);
    return prev + (sample - prev) * alpha;
}
static int32_t app_clip_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
// 同一帧可能被 UI/control 多次读取，计数器只允许处理一次。
static bool app_dock_is_repeated_frame(const app_vision_result_t *vision)
{
    return (vision != NULL) &&
           s_rt.have_processed_frame &&
           (s_rt.last_processed_frame_seq == vision->frame_seq);
}
static void app_dock_mark_frame_processed(const app_vision_result_t *vision)
{
    if (vision == NULL)
    {
        return;
    }
    s_rt.have_processed_frame = true;
    s_rt.last_processed_frame_seq = vision->frame_seq;
}
// 标签 ID 变化时重置滤波器；同一标签则持续 EMA 平滑。
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
// 由标签实际尺寸、焦距和像素边长估算相机到标签距离。
static int32_t app_dock_estimate_distance_mm(float edge_px)
{
    if (edge_px <= 1.0f || s_cfg.focal_length_px <= 1.0f || s_cfg.tag_size_mm <= 0)
    {
        return -1;
    }
    return (int32_t)((s_cfg.focal_length_px * (float)s_cfg.tag_size_mm / edge_px) + 0.5f);
}
// 把中心偏差、稳定度、靠近程度和距离门控合成 0~100 的悬停评分。
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
// 填充对接结果中的原始值、滤波值和派生距离。
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
// 默认参数偏演示友好，距离门控默认关闭以提高现场容错。
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
    out->center_x_tol = 100;
    out->center_y_tol = 75;
    out->min_area = 500;
    out->min_bbox_w = 24;
    out->min_bbox_h = 24;
    out->min_stable_count = 2;
    out->use_distance_gate = false;
    out->tag_size_mm = 60;
    out->focal_length_px = 314.0f;
    out->min_distance_mm = 120;
    out->max_distance_mm = 700;
    out->ema_shift = 2;
    out->ready_enter_frames = 1;
    out->ready_exit_bad_frames = 6;
    out->aligned_enter_frames = 1;
    out->wrong_id_enter_frames = 2;
    out->lost_hold_frames = 6;
}
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg)
{
    if (cfg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    app_dock_judge_reset();
    s_inited = true;
    return ESP_OK;
}
// 云端或任务更新目标 ID 后重置历史滤波/滞回状态。
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
    s_rt.have_processed_frame = false;
    s_rt.last_processed_frame_seq = 0;
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;
    return ESP_OK;
}
void app_dock_judge_reset(void)
{
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;
}
// 核心判定入口：输入视觉结果，输出带滞回的对接状态。
bool app_dock_judge_process(const app_vision_result_t *vision,
    app_dock_judge_result_t *out)
{
    if (!s_inited || vision == NULL || out == NULL)
    {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const bool repeated_frame = app_dock_is_repeated_frame(vision);
    if (!vision->valid)
    {
        // 短时丢帧时保持上一次状态，避免 UI 和控制状态剧烈跳变。
        if (!repeated_frame && s_rt.invalid_hold_count < s_cfg.lost_hold_frames)
        {
            s_rt.invalid_hold_count++;
        }
        if (!repeated_frame)
        {
            s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);
            s_rt.ready_pass_count = 0;
            s_rt.aligned_pass_count = 0;
            s_rt.wrong_id_count = 0;
            app_dock_mark_frame_processed(vision);
        }
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
        if (!repeated_frame)
        {
            s_rt.have_filter = false;
            s_rt.last_tag_id = 0;
            s_rt.last_state = APP_DOCK_STATE_SEARCHING;
        }
        return true;
    }
    if (!repeated_frame)
    {
        s_rt.invalid_hold_count = 0;
        app_dock_apply_filter(vision);
        app_dock_mark_frame_processed(vision);
    }
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
        // 错误 ID 需要连续出现才进入 wrong_id，降低误检噪声影响。
        if (!repeated_frame)
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
        }
        out->state = s_rt.last_state;
        out->hover_score = app_dock_calc_hover_score(out);
        out->ready_pass_count = s_rt.ready_pass_count;
        out->ready_bad_count = s_rt.ready_bad_count;
        out->invalid_hold_count = s_rt.invalid_hold_count;
        return true;
    }
    s_rt.wrong_id_count = 0;
    // ready 条件比 aligned 更严格，ready 状态退出也带坏帧滞回。
    const bool ready_cond = out->centered_ok &&
    out->near_ok &&
    out->stable_ok &&
    (!s_cfg.use_distance_gate || out->distance_ok);
    const bool aligned_cond = out->centered_ok &&
    (out->near_ok || out->stable_ok);
    if (!repeated_frame)
    {
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
    }
    out->state = s_rt.last_state;
    out->hover_score = app_dock_calc_hover_score(out);
    out->ready_pass_count = s_rt.ready_pass_count;
    out->ready_bad_count = s_rt.ready_bad_count;
    out->invalid_hold_count = s_rt.invalid_hold_count;
    return true;
}
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
// 简短状态文案给主状态栏使用。
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
