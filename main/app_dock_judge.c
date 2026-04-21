/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */





















/* 引入本项目的 app_dock_judge 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_dock_judge.h"

/* 引入 stdio.h；标准输入输出头文件；常见的 printf、snprintf 等格式化输出接口都在这里声明。 */
#include <stdio.h>
/* 引入 string.h；标准字符串/内存处理头文件；memcpy、memset、strcmp 等基础接口都来自这里。 */
#include <string.h>

/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"















/* 这里把 "app_dock" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_dock";







/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这里先定义变量 have_filter，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool have_filter;

    /* 这里先定义变量 last_tag_id，类型是 uint16_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint16_t last_tag_id;

    /* 这里先定义变量 ready_pass_count，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t ready_pass_count;

    /* 这里先定义变量 ready_bad_count，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t ready_bad_count;

    /* 这里先定义变量 aligned_pass_count，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t aligned_pass_count;

    /* 这里先定义变量 wrong_id_count，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t wrong_id_count;

    /* 这里先定义变量 invalid_hold_count，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t invalid_hold_count;


    /* 这里先定义变量 filtered_center_x，类型是 int32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    int32_t filtered_center_x;

    /* 这里先定义变量 filtered_center_y，类型是 int32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    int32_t filtered_center_y;

    /* 这里先定义变量 filtered_area，类型是 int32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    int32_t filtered_area;

    /* 这里先定义变量 filtered_bbox_w，类型是 int32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    int32_t filtered_bbox_w;

    /* 这里先定义变量 filtered_bbox_h，类型是 int32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    int32_t filtered_bbox_h;

    /* 这里先定义变量 filtered_edge_px，类型是 float；后面真正给它赋值或填内容的代码会继续跟上。 */
    float filtered_edge_px;

    /* 这里先定义变量 filtered_angle_deg，类型是 float；后面真正给它赋值或填内容的代码会继续跟上。 */
    float filtered_angle_deg;


    /* 这里先定义变量 last_state，类型是 app_dock_state_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_dock_state_t last_state;

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_dock_judge_runtime_t;


/* 这里定义变量 s_cfg，类型是 static app_dock_judge_config_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_dock_judge_config_t s_cfg = {0};

/* 这里定义变量 s_rt，类型是 static app_dock_judge_runtime_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_dock_judge_runtime_t s_rt = {0};

/* 这里定义变量 s_inited，类型是 static bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
static bool s_inited = false;






/* 这里开始定义函数 app_abs_i32；返回类型是 static inline int32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline int32_t app_abs_i32(int32_t v)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (v >= 0) ? v : -v 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (v >= 0) ? v : -v;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 这里开始定义函数 app_sat_inc_u8；返回类型是 static inline uint8_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline uint8_t app_sat_inc_u8(uint8_t v)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (v == UINT8_MAX) ? UINT8_MAX : (uint8_t)(v + 1U) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (v == UINT8_MAX) ? UINT8_MAX : (uint8_t)(v + 1U);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_filter_ema_i32；返回类型是 static int32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static int32_t app_filter_ema_i32(int32_t prev, int32_t sample, uint8_t shift)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 shift == 0U；只有条件成立，后面的分支代码才会执行。 */
    if (shift == 0U) {

        /* 这里把 sample 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return sample;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 delta，类型是 int32_t，并且在声明时就把初值设成 sample - prev；这样后面第一次使用它时就是一个确定状态。 */
    int32_t delta = sample - prev;

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    return prev + ((delta >= 0) ? ((delta + (1 << (shift - 1))) >> shift)
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                : -(((-delta) + (1 << (shift - 1))) >> shift));
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_filter_ema_f32；返回类型是 static float，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static float app_filter_ema_f32(float prev, float sample, uint8_t shift)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 shift == 0U；只有条件成立，后面的分支代码才会执行。 */
    if (shift == 0U) {

        /* 这里把 sample 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return sample;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 alpha，类型是 const float，并且在声明时就把初值设成 1.0f / (float)(1U << shift)；这样后面第一次使用它时就是一个确定状态。 */
    const float alpha = 1.0f / (float)(1U << shift);

    /* 这里把 prev + (sample - prev) * alpha 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return prev + (sample - prev) * alpha;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_clip_i32；返回类型是 static int32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static int32_t app_clip_i32(int32_t v, int32_t lo, int32_t hi)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 v < lo；只有条件成立，后面的分支代码才会执行。 */
    if (v < lo) return lo;

    /* 这里开始判断条件 v > hi；只有条件成立，后面的分支代码才会执行。 */
    if (v > hi) return hi;

    /* 这里把 v 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return v;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_apply_filter；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_dock_apply_filter(const app_vision_result_t *vision)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_rt.have_filter || (s_rt.last_tag_id != vision->tag_id)；只有条件成立，后面的分支代码才会执行。 */
    if (!s_rt.have_filter || (s_rt.last_tag_id != vision->tag_id)) {

        /* 这里把 true 写入 s、rt、have、filter；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.have_filter = true;

        /* 这里把 vision->tag_id 写入 s、rt、last、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.last_tag_id = vision->tag_id;

        /* 这里把 vision->center_x 写入 s、rt、filtered、center、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_center_x = vision->center_x;

        /* 这里把 vision->center_y 写入 s、rt、filtered、center、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_center_y = vision->center_y;

        /* 这里把 vision->area 写入 s、rt、filtered、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_area = vision->area;

        /* 这里把 vision->bbox_w 写入 s、rt、filtered、bbox、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_bbox_w = vision->bbox_w;

        /* 这里把 vision->bbox_h 写入 s、rt、filtered、bbox、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_bbox_h = vision->bbox_h;

        /* 这里把 vision->edge_px_avg 写入 s、rt、filtered、edge、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_edge_px = vision->edge_px_avg;

        /* 这里把 vision->top_edge_angle_deg 写入 s、rt、filtered、angle、deg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.filtered_angle_deg = vision->top_edge_angle_deg;

        /* 这里把 0 写入 s、rt、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_pass_count = 0;

        /* 这里把 0 写入 s、rt、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_bad_count = 0;

        /* 这里把 0 写入 s、rt、aligned、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.aligned_pass_count = 0;

        /* 这里把 0 写入 s、rt、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.invalid_hold_count = 0;

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_filter_ema_i32(s_rt.filtered_center_x, 写入 s、rt、filtered、center、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_center_x = app_filter_ema_i32(s_rt.filtered_center_x,
                                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                vision->center_x,
                                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                s_cfg.ema_shift);

    /* 这里把 app_filter_ema_i32(s_rt.filtered_center_y, 写入 s、rt、filtered、center、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_center_y = app_filter_ema_i32(s_rt.filtered_center_y,
                                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                vision->center_y,
                                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                s_cfg.ema_shift);

    /* 这里把 app_filter_ema_i32(s_rt.filtered_area, 写入 s、rt、filtered、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_area = app_filter_ema_i32(s_rt.filtered_area,
                                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                            vision->area,
                                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                            s_cfg.ema_shift);

    /* 这里把 app_filter_ema_i32(s_rt.filtered_bbox_w, 写入 s、rt、filtered、bbox、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_bbox_w = app_filter_ema_i32(s_rt.filtered_bbox_w,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              vision->bbox_w,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              s_cfg.ema_shift);

    /* 这里把 app_filter_ema_i32(s_rt.filtered_bbox_h, 写入 s、rt、filtered、bbox、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_bbox_h = app_filter_ema_i32(s_rt.filtered_bbox_h,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              vision->bbox_h,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              s_cfg.ema_shift);

    /* 这里把 app_filter_ema_f32(s_rt.filtered_edge_px, 写入 s、rt、filtered、edge、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_edge_px = app_filter_ema_f32(s_rt.filtered_edge_px,
                                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                               vision->edge_px_avg,
                                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                               s_cfg.ema_shift);

    /* 这里把 app_filter_ema_f32(s_rt.filtered_angle_deg, 写入 s、rt、filtered、angle、deg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.filtered_angle_deg = app_filter_ema_f32(s_rt.filtered_angle_deg,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 vision->top_edge_angle_deg,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 s_cfg.ema_shift);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_estimate_distance_mm；返回类型是 static int32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static int32_t app_dock_estimate_distance_mm(float edge_px)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 edge_px <= 1.0f || s_cfg.focal_length_px <= 1.0f || s_cfg.tag_size_mm <= 0；只有条件成立，后面的分支代码才会执行。 */
    if (edge_px <= 1.0f || s_cfg.focal_length_px <= 1.0f || s_cfg.tag_size_mm <= 0) {

        /* 这里把 -1 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return -1;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 (int32_t)((s_cfg.focal_length_px * (float)s_cfg.tag_size_mm / edge_px) + 0.5f) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (int32_t)((s_cfg.focal_length_px * (float)s_cfg.tag_size_mm / edge_px) + 0.5f);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_calc_hover_score；返回类型是 static uint8_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static uint8_t app_dock_calc_hover_score(const app_dock_judge_result_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 center_x_score，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t center_x_score = 0;

    /* 这里定义变量 center_y_score，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t center_y_score = 0;

    /* 这里定义变量 stable_score，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t stable_score = 0;

    /* 这里定义变量 near_score，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t near_score = 0;

    /* 这里定义变量 dist_score，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t dist_score = 0;


    /* 这里定义变量 x_den，类型是 const int32_t，并且在声明时就把初值设成 (s_cfg.center_x_tol > 0) ? (s_cfg.center_x_tol * 2) : 1；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t x_den = (s_cfg.center_x_tol > 0) ? (s_cfg.center_x_tol * 2) : 1;

    /* 这里定义变量 y_den，类型是 const int32_t，并且在声明时就把初值设成 (s_cfg.center_y_tol > 0) ? (s_cfg.center_y_tol * 2) : 1；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t y_den = (s_cfg.center_y_tol > 0) ? (s_cfg.center_y_tol * 2) : 1;


    /* 这里把 100 - app_clip_i32((app_abs_i32(out->dx) * 100) / x_den, 0, 100) 写入 center、x、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    center_x_score = 100 - app_clip_i32((app_abs_i32(out->dx) * 100) / x_den, 0, 100);

    /* 这里把 100 - app_clip_i32((app_abs_i32(out->dy) * 100) / y_den, 0, 100) 写入 center、y、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    center_y_score = 100 - app_clip_i32((app_abs_i32(out->dy) * 100) / y_den, 0, 100);


    /* 这里开始判断条件 s_cfg.min_stable_count > 0；只有条件成立，后面的分支代码才会执行。 */
    if (s_cfg.min_stable_count > 0) {

        /* 这里把 app_clip_i32(((int32_t)out->stable_count * 100) / (int32_t)s_cfg.min_stable_count, 0, 100) 写入 stable、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        stable_score = app_clip_i32(((int32_t)out->stable_count * 100) / (int32_t)s_cfg.min_stable_count, 0, 100);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_cfg.min_area > 0；只有条件成立，后面的分支代码才会执行。 */
    if (s_cfg.min_area > 0) {

        /* 这里把 app_clip_i32((out->filtered_area * 100) / s_cfg.min_area, 0, 100) 写入 near、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        near_score = app_clip_i32((out->filtered_area * 100) / s_cfg.min_area, 0, 100);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 out->est_distance_mm > 0；只有条件成立，后面的分支代码才会执行。 */
    if (out->est_distance_mm > 0) {

        /* 这里开始判断条件 s_cfg.use_distance_gate；只有条件成立，后面的分支代码才会执行。 */
        if (s_cfg.use_distance_gate) {

            /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
            if (out->est_distance_mm >= s_cfg.min_distance_mm &&
                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                out->est_distance_mm <= s_cfg.max_distance_mm) {
                /* 这里把 100 写入 dist、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                dist_score = 100;
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 这里定义变量 target_mid，类型是 const int32_t，并且在声明时就把初值设成 (s_cfg.min_distance_mm + s_cfg.max_distance_mm) / 2；这样后面第一次使用它时就是一个确定状态。 */
                const int32_t target_mid = (s_cfg.min_distance_mm + s_cfg.max_distance_mm) / 2;

                /* 这里定义变量 tol，类型是 const int32_t，并且在声明时就把初值设成 ((s_cfg.max_distance_mm - s_cfg.min_distance_mm) / 2) + 1；这样后面第一次使用它时就是一个确定状态。 */
                const int32_t tol = ((s_cfg.max_distance_mm - s_cfg.min_distance_mm) / 2) + 1;

                /* 这里把 100 - app_clip_i32((app_abs_i32(out->est_distance_mm - target_mid) * 100) / tol, 0, 100) 写入 dist、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                dist_score = 100 - app_clip_i32((app_abs_i32(out->est_distance_mm - target_mid) * 100) / tol, 0, 100);
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 这里把 100 写入 dist、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            dist_score = 100;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里定义变量 score，类型是 int32_t，并且在声明时就把初值设成 ((center_x_score + center_y_score) / 2) * 35；这样后面第一次使用它时就是一个确定状态。 */
    int32_t score = ((center_x_score + center_y_score) / 2) * 35
                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                  + stable_score * 25
                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                  + near_score * 20

                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                  + dist_score * 20;

    /* 这里把 100 写入 score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    score /= 100;


    /* 这里开始判断条件 !out->target_id_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!out->target_id_ok) {

        /* 这里把 4 写入 score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        score /= 4;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 !out->vision_valid；只有条件成立，后面的分支代码才会执行。 */
    if (!out->vision_valid) {

        /* 这里把 0 写入 score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        score = 0;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 (uint8_t)app_clip_i32(score, 0, 100) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint8_t)app_clip_i32(score, 0, 100);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_fill_result_base；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_dock_fill_result_base(const app_vision_result_t *vision,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      app_dock_judge_result_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 vision->valid 写入 out、视觉、valid；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->vision_valid = vision->valid;

    /* 这里把 vision->tag_id 写入 out、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->tag_id = vision->tag_id;

    /* 这里把 vision->frame_seq 写入 out、帧、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->frame_seq = vision->frame_seq;

    /* 这里把 vision->area 写入 out、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->area = vision->area;

    /* 这里把 vision->bbox_w 写入 out、bbox、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->bbox_w = vision->bbox_w;

    /* 这里把 vision->bbox_h 写入 out、bbox、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->bbox_h = vision->bbox_h;

    /* 这里把 vision->stable_count 写入 out、stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->stable_count = vision->stable_count;

    /* 这里把 vision->lost_count 写入 out、lost、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->lost_count = vision->lost_count;

    /* 这里把 vision->center_x - s_cfg.center_x_ref 写入 out、raw、dx；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->raw_dx = vision->center_x - s_cfg.center_x_ref;

    /* 这里把 vision->center_y - s_cfg.center_y_ref 写入 out、raw、dy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->raw_dy = vision->center_y - s_cfg.center_y_ref;

    /* 这里把 vision->area 写入 out、raw、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->raw_area = vision->area;

    /* 这里把 vision->edge_px_avg 写入 out、raw、edge、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->raw_edge_px = vision->edge_px_avg;


    /* 这里把 s_rt.filtered_center_x 写入 out、filtered、center、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->filtered_center_x = s_rt.filtered_center_x;

    /* 这里把 s_rt.filtered_center_y 写入 out、filtered、center、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->filtered_center_y = s_rt.filtered_center_y;

    /* 这里把 s_rt.filtered_area 写入 out、filtered、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->filtered_area = s_rt.filtered_area;

    /* 这里把 s_rt.filtered_edge_px 写入 out、filtered、edge、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->filtered_edge_px = s_rt.filtered_edge_px;

    /* 这里把 s_rt.filtered_angle_deg 写入 out、angle、deg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->angle_deg = s_rt.filtered_angle_deg;

    /* 这里把 s_rt.filtered_center_x - s_cfg.center_x_ref 写入 out、dx；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->dx = s_rt.filtered_center_x - s_cfg.center_x_ref;

    /* 这里把 s_rt.filtered_center_y - s_cfg.center_y_ref 写入 out、dy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->dy = s_rt.filtered_center_y - s_cfg.center_y_ref;

    /* 这里把 s_rt.ready_pass_count 写入 out、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ready_pass_count = s_rt.ready_pass_count;

    /* 这里把 s_rt.ready_bad_count 写入 out、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ready_bad_count = s_rt.ready_bad_count;

    /* 这里把 s_rt.invalid_hold_count 写入 out、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->invalid_hold_count = s_rt.invalid_hold_count;

    /* 这里把 app_dock_estimate_distance_mm(s_rt.filtered_edge_px) 写入 out、est、距离、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->est_distance_mm = app_dock_estimate_distance_mm(s_rt.filtered_edge_px);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_get_default_config；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_dock_judge_get_default_config(app_dock_judge_config_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (out == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(out, 0, sizeof(*out));









    /* 这里把 true 写入 out、use、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->use_target_id = true;

    /* 这里把 1 写入 out、目标、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->target_tag_id = 1;

    /* 这里把 120 写入 out、center、x、ref；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->center_x_ref = 120;

    /* 这里把 90 写入 out、center、y、ref；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->center_y_ref = 90;

    /* 这里把 28 写入 out、center、x、tol；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->center_x_tol = 28;

    /* 这里把 18 写入 out、center、y、tol；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->center_y_tol = 18;

    /* 这里把 2400 写入 out、最小、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->min_area = 2400;

    /* 这里把 58 写入 out、最小、bbox、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->min_bbox_w = 58;

    /* 这里把 72 写入 out、最小、bbox、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->min_bbox_h = 72;

    /* 这里把 5 写入 out、最小、stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->min_stable_count = 5;


    /* 这里把 true 写入 out、use、距离、门限；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->use_distance_gate = true;

    /* 这里把 100 写入 out、标签、大小、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->tag_size_mm = 100;

    /* 这里把 330.0f 写入 out、焦距、长度、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->focal_length_px = 330.0f;

    /* 这里把 220 写入 out、最小、距离、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->min_distance_mm = 220;

    /* 这里把 650 写入 out、最大、距离、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->max_distance_mm = 650;


    /* 这里把 2 写入 out、ema、shift；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ema_shift = 2;

    /* 这里把 2 写入 out、就绪、enter、frames；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ready_enter_frames = 2;

    /* 这里把 3 写入 out、就绪、exit、bad、frames；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ready_exit_bad_frames = 3;

    /* 这里把 1 写入 out、aligned、enter、frames；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->aligned_enter_frames = 1;

    /* 这里把 2 写入 out、wrong、ID、enter、frames；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->wrong_id_enter_frames = 2;

    /* 这里把 3 写入 out、lost、hold、frames；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->lost_hold_frames = 3;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_init；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_dock_judge_init(const app_dock_judge_config_t *cfg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 cfg == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (cfg == NULL) {

        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 *cfg 写入 s、配置；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cfg = *cfg;

    /* 调用本项目模块接口 app_dock_judge_reset；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_dock_judge_reset();

    /* 这里把 true 写入 s、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_inited = true;


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "dock init: target_en=%d target_id=%u ref=(%ld,%ld) tol=(%ld,%ld) area>=%ld bbox>=(%ld,%ld) stable>=%u tag=%ldmm focal=%.1fpx dist_gate=%d dist=[%ld,%ld]",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             s_cfg.use_target_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)s_cfg.target_tag_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.center_x_ref,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.center_y_ref,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.center_x_tol,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.center_y_tol,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.min_area,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.min_bbox_w,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.min_bbox_h,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)s_cfg.min_stable_count,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.tag_size_mm,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (double)s_cfg.focal_length_px,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             s_cfg.use_distance_gate,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.min_distance_mm,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)s_cfg.max_distance_mm);


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_get_config；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_dock_judge_get_config(app_dock_judge_config_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_inited || out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (!s_inited || out == NULL) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 s_cfg 写入 out；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *out = s_cfg;

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_set_target_id；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_dock_judge_set_target_id(uint16_t target_tag_id, bool enable_filter)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_inited；只有条件成立，后面的分支代码才会执行。 */
    if (!s_inited) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 target_tag_id 写入 s、配置、目标、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cfg.target_tag_id = target_tag_id;

    /* 这里把 enable_filter 写入 s、配置、use、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cfg.use_target_id = enable_filter;


    /* 这里把 false 写入 s、rt、have、filter；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.have_filter = false;

    /* 这里把 0 写入 s、rt、last、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_tag_id = 0;

    /* 这里把 0 写入 s、rt、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.ready_pass_count = 0;

    /* 这里把 0 写入 s、rt、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.ready_bad_count = 0;

    /* 这里把 0 写入 s、rt、aligned、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.aligned_pass_count = 0;

    /* 这里把 0 写入 s、rt、wrong、ID、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.wrong_id_count = 0;

    /* 这里把 0 写入 s、rt、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.invalid_hold_count = 0;

    /* 这里把 APP_DOCK_STATE_SEARCHING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;


    /* 这里把 %d target_id=%u", s_cfg.use_target_id, (unsigned)s_cfg.target_tag_id) 写入 ESP、LOGI、标签、接驳、目标、updated、使能；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "dock target updated: enable=%d target_id=%u", s_cfg.use_target_id, (unsigned)s_cfg.target_tag_id);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_reset；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_dock_judge_reset(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_rt, 0, sizeof(s_rt));

    /* 这里把 APP_DOCK_STATE_SEARCHING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_state = APP_DOCK_STATE_SEARCHING;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_process；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_dock_judge_process(const app_vision_result_t *vision,
                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                            app_dock_judge_result_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_inited || vision == NULL || out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (!s_inited || vision == NULL || out == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(out, 0, sizeof(*out));


    /* 这里开始判断条件 !vision->valid；只有条件成立，后面的分支代码才会执行。 */
    if (!vision->valid) {

        /* 这里开始判断条件 s_rt.invalid_hold_count < s_cfg.lost_hold_frames；只有条件成立，后面的分支代码才会执行。 */
        if (s_rt.invalid_hold_count < s_cfg.lost_hold_frames) {

            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            s_rt.invalid_hold_count++;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里把 app_sat_inc_u8(s_rt.ready_bad_count) 写入 s、rt、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);

        /* 这里把 0 写入 s、rt、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_pass_count = 0;

        /* 这里把 0 写入 s、rt、aligned、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.aligned_pass_count = 0;

        /* 这里把 0 写入 s、rt、wrong、ID、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.wrong_id_count = 0;


        /* 这里把 false 写入 out、视觉、valid；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->vision_valid = false;

        /* 这里把 vision->frame_seq 写入 out、帧、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->frame_seq = vision->frame_seq;

        /* 这里把 vision->lost_count 写入 out、lost、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->lost_count = vision->lost_count;

        /* 这里把 s_rt.invalid_hold_count 写入 out、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->invalid_hold_count = s_rt.invalid_hold_count;

        /* 这里把 s_rt.ready_bad_count 写入 out、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->ready_bad_count = s_rt.ready_bad_count;

        /* 这里把 APP_DOCK_STATE_SEARCHING 写入 out、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->state = APP_DOCK_STATE_SEARCHING;


        /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
        if (s_rt.last_state != APP_DOCK_STATE_SEARCHING &&
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            s_rt.invalid_hold_count < s_cfg.lost_hold_frames) {
            /* 这里把 s_rt.last_state 写入 out、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->state = s_rt.last_state;

            /* 这里把 s_rt.filtered_center_x 写入 out、filtered、center、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->filtered_center_x = s_rt.filtered_center_x;

            /* 这里把 s_rt.filtered_center_y 写入 out、filtered、center、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->filtered_center_y = s_rt.filtered_center_y;

            /* 这里把 s_rt.filtered_area 写入 out、filtered、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->filtered_area = s_rt.filtered_area;

            /* 这里把 s_rt.filtered_edge_px 写入 out、filtered、edge、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->filtered_edge_px = s_rt.filtered_edge_px;

            /* 这里把 s_rt.filtered_angle_deg 写入 out、angle、deg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->angle_deg = s_rt.filtered_angle_deg;

            /* 这里把 s_rt.filtered_center_x - s_cfg.center_x_ref 写入 out、dx；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->dx = s_rt.filtered_center_x - s_cfg.center_x_ref;

            /* 这里把 s_rt.filtered_center_y - s_cfg.center_y_ref 写入 out、dy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->dy = s_rt.filtered_center_y - s_cfg.center_y_ref;

            /* 这里把 app_dock_estimate_distance_mm(s_rt.filtered_edge_px) 写入 out、est、距离、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->est_distance_mm = app_dock_estimate_distance_mm(s_rt.filtered_edge_px);

            /* 这里把 0 写入 out、hover、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->hover_score = 0;

            /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return true;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里把 false 写入 s、rt、have、filter；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.have_filter = false;

        /* 这里把 0 写入 s、rt、last、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.last_tag_id = 0;

        /* 这里把 APP_DOCK_STATE_SEARCHING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.last_state = APP_DOCK_STATE_SEARCHING;

        /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 0 写入 s、rt、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.invalid_hold_count = 0;

    /* 调用本项目模块接口 app_dock_apply_filter；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_dock_apply_filter(vision);

    /* 调用本项目模块接口 app_dock_fill_result_base；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_dock_fill_result_base(vision, out);


    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    out->target_id_ok = (!s_cfg.use_target_id) || (vision->tag_id == s_cfg.target_tag_id);

    /* 调用本项目模块接口 app_abs_i32；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    out->centered_ok = (app_abs_i32(out->dx) <= s_cfg.center_x_tol) &&

                       /* 调用本项目模块接口 app_abs_i32；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                       (app_abs_i32(out->dy) <= s_cfg.center_y_tol);

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    out->near_ok = (s_rt.filtered_area >= s_cfg.min_area) &&
                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                   (s_rt.filtered_bbox_w >= s_cfg.min_bbox_w) &&

                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                   (s_rt.filtered_bbox_h >= s_cfg.min_bbox_h);

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    out->stable_ok = (vision->stable_count >= s_cfg.min_stable_count);


    /* 这里开始判断条件 out->est_distance_mm > 0；只有条件成立，后面的分支代码才会执行。 */
    if (out->est_distance_mm > 0) {

        /* 这里开始判断条件 s_cfg.use_distance_gate；只有条件成立，后面的分支代码才会执行。 */
        if (s_cfg.use_distance_gate) {

            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            out->distance_ok = (out->est_distance_mm >= s_cfg.min_distance_mm) &&

                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                               (out->est_distance_mm <= s_cfg.max_distance_mm);
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 这里把 true 写入 out、距离、成功；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            out->distance_ok = true;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 这里把 false 写入 out、距离、成功；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->distance_ok = false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !out->target_id_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!out->target_id_ok) {

        /* 这里把 app_sat_inc_u8(s_rt.wrong_id_count) 写入 s、rt、wrong、ID、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.wrong_id_count = app_sat_inc_u8(s_rt.wrong_id_count);

        /* 这里把 0 写入 s、rt、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_pass_count = 0;

        /* 这里把 0 写入 s、rt、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_bad_count = 0;

        /* 这里把 0 写入 s、rt、aligned、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.aligned_pass_count = 0;


        /* 这里开始判断条件 s_rt.wrong_id_count >= s_cfg.wrong_id_enter_frames；只有条件成立，后面的分支代码才会执行。 */
        if (s_rt.wrong_id_count >= s_cfg.wrong_id_enter_frames) {

            /* 这里把 APP_DOCK_STATE_WRONG_ID 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_state = APP_DOCK_STATE_WRONG_ID;
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 这里把 APP_DOCK_STATE_TRACKING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_state = APP_DOCK_STATE_TRACKING;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里把 s_rt.last_state 写入 out、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->state = s_rt.last_state;

        /* 这里把 app_dock_calc_hover_score(out) 写入 out、hover、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->hover_score = app_dock_calc_hover_score(out);

        /* 这里把 s_rt.ready_pass_count 写入 out、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->ready_pass_count = s_rt.ready_pass_count;

        /* 这里把 s_rt.ready_bad_count 写入 out、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->ready_bad_count = s_rt.ready_bad_count;

        /* 这里把 s_rt.invalid_hold_count 写入 out、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out->invalid_hold_count = s_rt.invalid_hold_count;

        /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 0 写入 s、rt、wrong、ID、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.wrong_id_count = 0;

    /* 这里定义变量 ready_cond，类型是 const bool，并且在声明时就把初值设成 out->centered_ok &&；这样后面第一次使用它时就是一个确定状态。 */
    const bool ready_cond = out->centered_ok &&
                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                            out->near_ok &&
                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                            out->stable_ok &&

                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                            (!s_cfg.use_distance_gate || out->distance_ok);

    /* 这里定义变量 aligned_cond，类型是 const bool，并且在声明时就把初值设成 out->centered_ok &&；这样后面第一次使用它时就是一个确定状态。 */
    const bool aligned_cond = out->centered_ok &&

                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                              (out->near_ok || out->stable_ok);


    /* 这里开始判断条件 ready_cond；只有条件成立，后面的分支代码才会执行。 */
    if (ready_cond) {

        /* 这里把 app_sat_inc_u8(s_rt.ready_pass_count) 写入 s、rt、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_pass_count = app_sat_inc_u8(s_rt.ready_pass_count);

        /* 这里把 0 写入 s、rt、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_bad_count = 0;
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 这里把 0 写入 s、rt、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_pass_count = 0;

        /* 这里把 app_sat_inc_u8(s_rt.ready_bad_count) 写入 s、rt、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ready_bad_count = app_sat_inc_u8(s_rt.ready_bad_count);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 aligned_cond；只有条件成立，后面的分支代码才会执行。 */
    if (aligned_cond) {

        /* 这里把 app_sat_inc_u8(s_rt.aligned_pass_count) 写入 s、rt、aligned、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.aligned_pass_count = app_sat_inc_u8(s_rt.aligned_pass_count);
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 这里把 0 写入 s、rt、aligned、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.aligned_pass_count = 0;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (s_rt.last_state) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_READY_TO_DOCK:

            /* 这里开始判断条件 ready_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames)；只有条件成立，后面的分支代码才会执行。 */
            if (ready_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames)) {

                /* 这里把 APP_DOCK_STATE_READY_TO_DOCK 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            } else if (aligned_cond) {

                /* 这里把 APP_DOCK_STATE_ALIGNED 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_ALIGNED;
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 这里把 APP_DOCK_STATE_TRACKING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_TRACKING;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_ALIGNED:

            /* 这里开始判断条件 ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames)；只有条件成立，后面的分支代码才会执行。 */
            if (ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames)) {

                /* 这里把 APP_DOCK_STATE_READY_TO_DOCK 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            } else if (aligned_cond || (s_rt.ready_bad_count < s_cfg.ready_exit_bad_frames)) {

                /* 这里把 APP_DOCK_STATE_ALIGNED 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_ALIGNED;
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 这里把 APP_DOCK_STATE_TRACKING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_TRACKING;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_TRACKING:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_SEARCHING:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_WRONG_ID:

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 这里开始判断条件 ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames)；只有条件成立，后面的分支代码才会执行。 */
            if (ready_cond && (s_rt.ready_pass_count >= s_cfg.ready_enter_frames)) {

                /* 这里把 APP_DOCK_STATE_READY_TO_DOCK 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_READY_TO_DOCK;
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            } else if (aligned_cond && (s_rt.aligned_pass_count >= s_cfg.aligned_enter_frames)) {

                /* 这里把 APP_DOCK_STATE_ALIGNED 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_ALIGNED;
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 这里把 APP_DOCK_STATE_TRACKING 写入 s、rt、last、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_state = APP_DOCK_STATE_TRACKING;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 s_rt.last_state 写入 out、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->state = s_rt.last_state;

    /* 这里把 app_dock_calc_hover_score(out) 写入 out、hover、score；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->hover_score = app_dock_calc_hover_score(out);

    /* 这里把 s_rt.ready_pass_count 写入 out、就绪、pass、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ready_pass_count = s_rt.ready_pass_count;

    /* 这里把 s_rt.ready_bad_count 写入 out、就绪、bad、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->ready_bad_count = s_rt.ready_bad_count;

    /* 这里把 s_rt.invalid_hold_count 写入 out、invalid、hold、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->invalid_hold_count = s_rt.invalid_hold_count;

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 调用本项目模块接口 app_dock_judge_state_to_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
const char *app_dock_judge_state_to_text(app_dock_state_t state)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (state) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_SEARCHING:

            /* 这里把 "searching" 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return "searching";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_WRONG_ID:

            /* 这里把 "wrong_id" 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return "wrong_id";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_TRACKING:

            /* 这里把 "tracking" 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return "tracking";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_ALIGNED:

            /* 这里把 "aligned" 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return "aligned";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_READY_TO_DOCK:

            /* 这里把 "ready" 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return "ready";

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 这里把 "unknown" 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return "unknown";
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_format_status；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_dock_judge_format_status(const app_dock_judge_result_t *result,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  char *buf,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  size_t buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 result == NULL || buf == NULL || buf_len == 0；只有条件成立，后面的分支代码才会执行。 */
    if (result == NULL || buf == NULL || buf_len == 0) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (result->state) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_SEARCHING:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: searching target");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_WRONG_ID:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: wrong tag id");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_TRACKING:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: target tracking");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_ALIGNED:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: aligned / hold");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_READY_TO_DOCK:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: ready to dock");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: unknown");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_dock_judge_format_detail；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_dock_judge_format_detail(const app_dock_judge_result_t *result,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  char *buf,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  size_t buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 result == NULL || buf == NULL || buf_len == 0；只有条件成立，后面的分支代码才会执行。 */
    if (result == NULL || buf == NULL || buf_len == 0) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !result->vision_valid；只有条件成立，后面的分支代码才会执行。 */
    if (!result->vision_valid) {

        /* 这里开始判断条件 result->state != APP_DOCK_STATE_SEARCHING；只有条件成立，后面的分支代码才会执行。 */
        if (result->state != APP_DOCK_STATE_SEARCHING) {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ld e:%.1f",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)result->invalid_hold_count,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)result->lost_count,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (long)result->dx,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (long)result->dy,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (long)result->est_distance_mm,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (double)result->filtered_edge_px);
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(buf,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             buf_len,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "dock dbg: id:%u dx:%ld dy:%ld z:%ldmm e:%.1f/%.1f ang:%d st:%u score:%u f:%c%c%c%c%c r:%u",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)result->tag_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)result->dx,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)result->dy,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)result->est_distance_mm,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (double)result->raw_edge_px,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (double)result->filtered_edge_px,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (int)result->angle_deg,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)result->stable_count,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)result->hover_score,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             result->target_id_ok ? 'I' : 'x',
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             result->centered_ok ? 'C' : 'x',
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             result->near_ok ? 'N' : 'x',
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             result->stable_ok ? 'S' : 'x',
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             result->distance_ok ? 'D' : 'x',
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)result->ready_pass_count);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
