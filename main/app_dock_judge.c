/*
 * app_dock_judge.c - 无人机接驳条件判定模块（详细注释版）
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

#include "app_dock_judge.h"                        // 项目自定义模块头文件，声明 app_dock_judge 对外提供的接口。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
static const char *TAG = "app_dock";                             // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
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
    app_dock_state_t last_state;
} app_dock_judge_runtime_t;
static app_dock_judge_config_t s_cfg = {0};                      // 模块级静态变量 s_cfg，只在本文件内部使用，避免被其他文件直接修改。
static app_dock_judge_runtime_t s_rt = {0};                      // 模块级静态变量 s_rt，只在本文件内部使用，避免被其他文件直接修改。
static bool s_inited = false;                                    // 模块级静态变量 s_inited，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 求 int32 绝对值，用于中心偏移和距离波动计算。
 */
static inline int32_t app_abs_i32(int32_t v)
{
    return (v >= 0) ? v : -v;
}
/*
 * 饱和自增 uint8，避免稳定计数超过最大值后溢出归零。
 */
static inline uint8_t app_sat_inc_u8(uint8_t v)
{
    return (v == UINT8_MAX) ? UINT8_MAX : (uint8_t)(v + 1U);
}
/*
 * 整数指数滑动平均滤波，用来平滑中心点、距离等抖动数据。
 */
static int32_t app_filter_ema_i32(int32_t prev, int32_t sample, uint8_t shift)
{
    if (shift == 0U) {
        return sample;
    }
    int32_t delta = sample - prev;
    return prev + ((delta >= 0) ? ((delta + (1 << (shift - 1))) >> shift)
                                : -(((-delta) + (1 << (shift - 1))) >> shift));
}
/*
 * 浮点指数滑动平均滤波，用来平滑边长或角度等浮点量。
 */
static float app_filter_ema_f32(float prev, float sample, uint8_t shift)
{
    if (shift == 0U) {
        return sample;
    }
    const float alpha = 1.0f / (float)(1U << shift);
    return prev + (sample - prev) * alpha;
}
/*
 * 把数值限制在指定范围内，避免异常输入影响判定。
 */
static int32_t app_clip_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
/*
 * 把视觉识别结果写入接驳运行时，并对中心点、距离等关键量做滤波。
 */
static void app_dock_apply_filter(const app_vision_result_t *vision)
{
    if (!s_rt.have_filter || (s_rt.last_tag_id != vision->tag_id)) {
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
/*
 * 根据标签真实尺寸、焦距和图像边长估算无人机距离。
 */
static int32_t app_dock_estimate_distance_mm(float edge_px)
{
    if (edge_px <= 1.0f || s_cfg.focal_length_px <= 1.0f || s_cfg.tag_size_mm <= 0) {
        return -1;
    }
    return (int32_t)((s_cfg.focal_length_px * (float)s_cfg.tag_size_mm / edge_px) + 0.5f);
}
/*
 * 把中心偏移、距离合法性、稳定帧数等信息换算成悬停稳定评分。
 */
static uint8_t app_dock_calc_hover_score(const app_dock_judge_result_t *out)
{
    int32_t center_x_score = 0;
    int32_t center_y_score = 0;
    int32_t stable_score = 0;
    int32_t near_score = 0;
    int32_t dist_score = 0;
    const int32_t x_den = (s_cfg.center_x_tol > 0) ? (s_cfg.center_x_tol * 2) : 1;
    const int32_t y_den = (s_cfg.center_y_tol > 0) ? (s_cfg.center_y_tol * 2) : 1;
/*
 * 把数值限制在指定范围内，避免异常输入影响判定。
 */
    center_x_score = 100 - app_clip_i32((app_abs_i32(out->dx) * 100) / x_den, 0, 100);
/*
 * 把数值限制在指定范围内，避免异常输入影响判定。
 */
    center_y_score = 100 - app_clip_i32((app_abs_i32(out->dy) * 100) / y_den, 0, 100);
    if (s_cfg.min_stable_count > 0) {
        stable_score = app_clip_i32(((int32_t)out->stable_count * 100) / (int32_t)s_cfg.min_stable_count, 0, 100);
    }
    if (s_cfg.min_area > 0) {
        near_score = app_clip_i32((out->filtered_area * 100) / s_cfg.min_area, 0, 100);
    }
    if (out->est_distance_mm > 0) {
        if (s_cfg.use_distance_gate) {
            if (out->est_distance_mm >= s_cfg.min_distance_mm &&
                out->est_distance_mm <= s_cfg.max_distance_mm) {
                dist_score = 100;
            } else {
                const int32_t target_mid = (s_cfg.min_distance_mm + s_cfg.max_distance_mm) / 2;
                const int32_t tol = ((s_cfg.max_distance_mm - s_cfg.min_distance_mm) / 2) + 1;
/*
 * 把数值限制在指定范围内，避免异常输入影响判定。
 */
                dist_score = 100 - app_clip_i32((app_abs_i32(out->est_distance_mm - target_mid) * 100) / tol, 0, 100);
            }
        } else {
            dist_score = 100;
        }
    }
    int32_t score = ((center_x_score + center_y_score) / 2) * 35
                  + stable_score * 25
                  + near_score * 20
                  + dist_score * 20;
    score /= 100;
    if (!out->target_id_ok) {
        score /= 4;
    }
    if (!out->vision_valid) {
        score = 0;
    }
    return (uint8_t)app_clip_i32(score, 0, 100);
}
/*
 * 把视觉结果和滤波后的运行时数据填入基础判定结果结构体。
 */
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
/*
 * 生成默认接驳判定参数，main.c 可以在此基础上修改目标 ID、距离阈值等。
 */
void app_dock_judge_get_default_config(app_dock_judge_config_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->use_target_id = true;
    out->target_tag_id = 1;
    out->center_x_ref = 120;
    out->center_y_ref = 90;
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
/*
 * 初始化接驳判定模块配置和运行时状态。
 */
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (cfg == NULL) {
        // 参数不合法时立即返回错误码，避免后面继续访问非法内存。
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    app_dock_judge_reset();
    s_inited = true;
    // 信息日志：用于确认程序执行到了哪个阶段。
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
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 读取当前接驳判定配置。
 */
esp_err_t app_dock_judge_get_config(app_dock_judge_config_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (!s_inited || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    *out = s_cfg;
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 运行时修改目标 tag ID，可用于云端/小程序切换订单目标。
 */
esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter)
{
    if (!s_inited) {
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
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "dock target updated: enable=%d target_id=%u", s_cfg.use_target_id, (unsigned)s_cfg.target_tag_id);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 清空滤波和状态机历史，重新开始判断。
 */
void app_dock_judge_reset(void)
{
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;
}
/*
 * 接驳判定核心函数：根据最新视觉结果更新状态机，并输出当前是否可接驳。
 */
bool app_dock_judge_process(const app_vision_result_t *vision,
                            app_dock_judge_result_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (!s_inited || vision == NULL || out == NULL) {
        return false;
    }

    /*
     * 每一帧都从干净的输出结构开始填充。
     *
     * app_ctrl.c 会根据 out 中的 state / ready_pass_count / distance_ok 等字段做控制决策，
     * 如果这里不清零，上一帧残留字段可能会把本帧误判成可接驳。
     */
    memset(out, 0, sizeof(*out));

    /*
     * 第一大类：这一帧没有有效视觉识别结果。
     *
     * 视觉丢帧不一定意味着无人机真的离开了画面，可能只是曝光、运动模糊或单帧识别失败。
     * 所以这里不是立刻把状态打回 searching，而是允许 lost_hold_frames 帧短暂保持上一状态，
     * 让 UI 和主控状态机不要因为 1~2 帧丢失而剧烈抖动。
     */
    if (!vision->valid) {
        /*
         * invalid_hold_count 记录连续无效帧数，但最多只增长到配置的 hold 窗口附近。
         */
        if (s_rt.invalid_hold_count < s_cfg.lost_hold_frames) {
            s_rt.invalid_hold_count++;
        }

        /*
         * 无效帧会削弱 ready 判定。
         *
         * ready_pass_count 清零表示“连续满足 ready 条件”的链条被打断；
         * ready_bad_count 增加用于 READY_TO_DOCK 状态的退出防抖。
         */
        s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);
        s_rt.ready_pass_count = 0;
        s_rt.aligned_pass_count = 0;
        s_rt.wrong_id_count = 0;

        /*
         * 即使视觉无效，也把基础调试字段写出去。
         * 这样 UI 可以显示 lost_count / hold_count，而不是完全没有信息。
         */
        out->vision_valid = false;
        out->frame_seq = vision->frame_seq;
        out->lost_count = vision->lost_count;
        out->invalid_hold_count = s_rt.invalid_hold_count;
        out->ready_bad_count = s_rt.ready_bad_count;
        out->state = APP_DOCK_STATE_SEARCHING;

        /*
         * 短暂丢帧保持上一状态。
         *
         * 例如上一帧已经 aligned，本帧突然没识别到 tag，
         * 如果 invalid_hold_count 还没有超过 lost_hold_frames，就继续输出上一状态和上一组滤波位置。
         * 这主要是为了 HUD 跟踪框和控制提示看起来稳定。
         */
        if (s_rt.last_state != APP_DOCK_STATE_SEARCHING &&
            s_rt.invalid_hold_count < s_cfg.lost_hold_frames) {
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

        /*
         * 丢帧超过 hold 窗口后，清掉滤波历史并回到 searching。
         * 下次重新识别到 tag 时，会重新建立滤波基准。
         */
        s_rt.have_filter = false;
        s_rt.last_tag_id = 0;
        s_rt.last_state = APP_DOCK_STATE_SEARCHING;
        return true;
    }

    /*
     * 第二大类：这一帧视觉识别有效。
     *
     * 有效帧会重置 invalid_hold_count，然后先更新滤波器，
     * 再根据滤波后的中心、面积、边长等字段进行工程判定。
     */
    s_rt.invalid_hold_count = 0;
    app_dock_apply_filter(vision);
    app_dock_fill_result_base(vision, out);

    /*
     * ID 判定。
     *
     * 如果 use_target_id=true，只有指定 target_tag_id 能通过；
     * 如果关闭目标 ID 过滤，则任意合法 AprilTag 都可以进入后续距离/稳定性判断。
     */
    out->target_id_ok = (!s_cfg.use_target_id) || (vision->tag_id == s_cfg.target_tag_id);

    /*
     * 中心对准判定。
     *
     * dx/dy 是滤波中心点相对于参考中心的偏移；
     * 只有 X/Y 都落在容差内，才认为无人机已经对准接收舱窗口。
     */
    out->centered_ok = (app_abs_i32(out->dx) <= s_cfg.center_x_tol) &&
                       (app_abs_i32(out->dy) <= s_cfg.center_y_tol);

    /*
     * 近距/尺寸判定。
     *
     * 面积和 bbox 尺寸太小，通常说明无人机离窗口太远，或者识别到了很小的噪声目标。
     */
    out->near_ok = (s_rt.filtered_area >= s_cfg.min_area) &&
                   (s_rt.filtered_bbox_w >= s_cfg.min_bbox_w) &&
                   (s_rt.filtered_bbox_h >= s_cfg.min_bbox_h);

    /*
     * 多帧稳定判定。
     *
     * app_vision.c 会对连续识别到同一 tag 的帧数计数；
     * 这里要求稳定帧数达到门槛，避免一闪而过的目标触发接驳。
     */
    out->stable_ok = (vision->stable_count >= s_cfg.min_stable_count);

    /*
     * 距离门限判定。
     *
     * est_distance_mm 来自标签实际边长、像素边长和等效焦距的粗估算。
     * 如果没有有效距离，则 distance_ok=false；如果关闭距离门限，只要能估出距离就放行。
     */
    if (out->est_distance_mm > 0) {
        if (s_cfg.use_distance_gate) {
            out->distance_ok = (out->est_distance_mm >= s_cfg.min_distance_mm) &&
                               (out->est_distance_mm <= s_cfg.max_distance_mm);
        } else {
            out->distance_ok = true;
        }
    } else {
        out->distance_ok = false;
    }

    /*
     * 目标 ID 不匹配时的处理。
     *
     * wrong_id 也做连续帧防抖：刚出现错误 ID 时先显示 tracking，
     * 连续达到 wrong_id_enter_frames 后才进入 WRONG_ID 状态。
     */
    if (!out->target_id_ok) {
        s_rt.wrong_id_count = app_sat_inc_u8(s_rt.wrong_id_count);
        s_rt.ready_pass_count = 0;
        s_rt.ready_bad_count = 0;
        s_rt.aligned_pass_count = 0;
        if (s_rt.wrong_id_count >= s_cfg.wrong_id_enter_frames) {
            s_rt.last_state = APP_DOCK_STATE_WRONG_ID;
        } else {
            s_rt.last_state = APP_DOCK_STATE_TRACKING;
        }
        out->state = s_rt.last_state;
        out->hover_score = app_dock_calc_hover_score(out);
        out->ready_pass_count = s_rt.ready_pass_count;
        out->ready_bad_count = s_rt.ready_bad_count;
        out->invalid_hold_count = s_rt.invalid_hold_count;
        return true;
    }

    /*
     * 走到这里说明 ID 正确。
     * 清空 wrong_id_count，开始计算 aligned / ready 两级条件。
     */
    s_rt.wrong_id_count = 0;

    /*
     * ready_cond 是真正允许接驳的严格条件：
     * - 目标居中；
     * - 目标足够近/足够大；
     * - 连续稳定帧数达标；
     * - 如果启用距离门限，距离也必须在安全范围内。
     */
    const bool ready_cond = out->centered_ok &&
                            out->near_ok &&
                            out->stable_ok &&
                            (!s_cfg.use_distance_gate || out->distance_ok);

    /*
     * aligned_cond 是比 ready 更宽松的中间状态。
     *
     * 只要中心已经对准，并且“足够近”或“稳定性”满足其一，
     * UI 就可以提示已经接近接驳条件，但还不一定允许真正开舱。
     */
    const bool aligned_cond = out->centered_ok &&
                              (out->near_ok || out->stable_ok);

    /*
     * ready_pass_count / ready_bad_count 是 ready 状态的进入和退出防抖。
     */
    if (ready_cond) {
        s_rt.ready_pass_count = app_sat_inc_u8(s_rt.ready_pass_count);
        s_rt.ready_bad_count = 0;
    } else {
        s_rt.ready_pass_count = 0;
        s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);
    }

    /*
     * aligned_pass_count 是 aligned 状态的进入防抖。
     */
    if (aligned_cond) {
        s_rt.aligned_pass_count = app_sat_inc_u8(s_rt.aligned_pass_count);
    } else {
        s_rt.aligned_pass_count = 0;
    }

    /*
     * 状态机更新。
     *
     * READY_TO_DOCK 有退出迟滞：短时间不满足 ready_cond 时不会立刻降级，
     * 需要 ready_bad_count 达到 ready_exit_bad_frames 才退出。
     */
    switch (s_rt.last_state) {
        case APP_DOCK_STATE_READY_TO_DOCK:
            if (ready_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames)) {
                s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
            } else if (aligned_cond) {
                s_rt.last_state = APP_DOCK_STATE_ALIGNED;
            } else {
                s_rt.last_state = APP_DOCK_STATE_TRACKING;
            }
            break;
        case APP_DOCK_STATE_ALIGNED:
            if (ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames)) {
                s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
            } else if (aligned_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames)) {
                s_rt.last_state = APP_DOCK_STATE_ALIGNED;
            } else {
                s_rt.last_state = APP_DOCK_STATE_TRACKING;
            }
            break;
        case APP_DOCK_STATE_TRACKING:
        case APP_DOCK_STATE_SEARCHING:
        case APP_DOCK_STATE_WRONG_ID:
        default:
            if (ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames)) {
                s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
            } else if (aligned_cond && (s_rt.aligned_pass_count >= s_cfg.aligned_enter_frames)) {
                s_rt.last_state = APP_DOCK_STATE_ALIGNED;
            } else {
                s_rt.last_state = APP_DOCK_STATE_TRACKING;
            }
            break;
    }

    /*
     * 把最终状态和防抖计数写入 out，供 UI 和 app_ctrl.c 使用。
     */
    out->state = s_rt.last_state;
    out->hover_score = app_dock_calc_hover_score(out);
    out->ready_pass_count = s_rt.ready_pass_count;
    out->ready_bad_count = s_rt.ready_bad_count;
    out->invalid_hold_count = s_rt.invalid_hold_count;
    return true;
}
/*
 * 把接驳状态枚举转换为 UI/日志可读字符串。
 */
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
/*
 * 把接驳判定结果格式化成短状态文本，适合显示在主状态栏。
 */
void app_dock_judge_format_status(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (result == NULL || buf == NULL || buf_len == 0) {
        return;
    }

    /*
     * 短状态文本只表达当前接驳阶段，不塞入太多调试数字。
     * 主状态栏空间有限，详细数值交给 app_dock_judge_format_detail()。
     */
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
/*
 * 把接驳判定结果格式化成详细调试文本，适合显示距离、偏移、分数等信息。
 */
void app_dock_judge_format_detail(const app_dock_judge_result_t *result,
                                  char *buf,
                                  size_t buf_len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (result == NULL || buf == NULL || buf_len == 0) {
        return;
    }

    /*
     * 视觉无效时分两种情况：
     * - 如果仍处于 hold 状态，显示上一帧滤波结果，便于观察短暂丢帧；
     * - 如果已经回到 searching，只提示等待有效 tag。
     */
    if (!result->vision_valid) {
        if (result->state != APP_DOCK_STATE_SEARCHING) {
            snprintf(buf,
                     buf_len,
                     "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ld e:%.1f",
                     (unsigned)result->invalid_hold_count,
                     (unsigned)result->lost_count,
                     (long)result->dx,
                     (long)result->dy,
                     (long)result->est_distance_mm,
                     (double)result->filtered_edge_px);
        } else {
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        }
        return;
    }

    /*
     * 视觉有效时输出完整调试字段：
     * - id：识别到的 tag；
     * - dx/dy：相对中心偏移；
     * - z：估算距离；
     * - e：原始/滤波边长；
     * - ang：顶边角度；
     * - st：稳定帧数；
     * - score：悬停质量分；
     * - f：各项判定标志 I/C/N/S/D；
     * - r：ready 连续通过帧数。
     */
    snprintf(buf,
             buf_len,
             "dock dbg: id:%u dx:%ld dy:%ld z:%ldmm e:%.1f/%.1f ang:%d st:%u score:%u f:%c%c%c%c%c r:%u",
             (unsigned)result->tag_id,
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
