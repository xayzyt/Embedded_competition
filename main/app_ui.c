/*
 * app_ui.c - LVGL 用户界面和视觉 HUD 覆盖层模块（详细注释版）
 *
 * 这个文件负责在 7 寸 MIPI 屏上显示系统状态：
 * - 状态栏、坐标、视觉识别信息、接驳调试信息；
 * - 中心十字准星、识别框、稳定锁定进度条；
 * - “鉴权通过”等提示横幅；
 * - 根据 app_dock_judge 的状态改变 HUD 颜色和提示。
 *
 * 注意：LVGL 不是线程安全的，所以任何跨任务更新 UI 的代码都必须通过 BSP/LVGL 的锁保护。
 * 本文件中的公开接口会先拿 bsp_display_lock() 再操作控件，避免 UI 刷新任务和其他任务同时改 LVGL 对象。
 */

#include "app_ui.h"                                // 项目自定义模块头文件，声明 app_ui 对外提供的接口。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <inttypes.h>                              // 跨平台整数格式化宏，方便日志打印固定宽度整数。
#include "lvgl.h"                                  // LVGL 图形库主头文件，提供控件、样式、画布、颜色等 API。
#include "esp_log.h"
#include "bsp/esp-bsp.h"                           // 乐鑫 BSP 通用接口，常用于显示、触摸、音频等板级资源。
#include "bsp/display.h"                           // BSP 显示接口和分辨率宏，例如 BSP_LCD_H_RES/BSP_LCD_V_RES。
#include "app_ai_capture.h"
#ifndef BSP_CAMERA_ROTATION
#define BSP_CAMERA_ROTATION 0
#endif
#define HUD_SRC_W               240                      // 屏幕 HUD 覆盖显示相关参数。
#define HUD_SRC_H               180                      // 屏幕 HUD 覆盖显示相关参数。
#define HUD_LOCK_SEG_COUNT      5                        // 屏幕 HUD 覆盖显示相关参数。
#define HUD_AUTH_SHOW_MS        1200                     // 屏幕 HUD 覆盖显示相关参数。
static const char *TAG = "app_ui";
static lv_obj_t *s_status = NULL;                                // 模块级静态变量 s_status，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_coord = NULL;                                 // 模块级静态变量 s_coord，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_vision = NULL;                                // 模块级静态变量 s_vision，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_dock = NULL;                                  // 模块级静态变量 s_dock，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_hint = NULL;                                  // 模块级静态变量 s_hint，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_hud_layer = NULL;                             // 模块级静态变量 s_hud_layer，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_track_box = NULL;                             // 模块级静态变量 s_track_box，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_cross_h = NULL;                               // 模块级静态变量 s_cross_h，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_cross_v = NULL;                               // 模块级静态变量 s_cross_v，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_lock_seg[HUD_LOCK_SEG_COUNT] = {0};           // 模块级静态变量 s_lock_seg，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_auth = NULL;                                  // 模块级静态变量 s_auth，只在本文件内部使用，避免被其他文件直接修改。
static lv_obj_t *s_cap_btn = NULL;
static lv_obj_t *s_stop_btn = NULL;
static lv_obj_t *s_mode_btn = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_capture = NULL;
static bool s_have_last_box = false;                             // 模块级静态变量 s_have_last_box，只在本文件内部使用，避免被其他文件直接修改。
static int32_t s_last_box_x = 0;                                 // 模块级静态变量 s_last_box_x，只在本文件内部使用，避免被其他文件直接修改。
static int32_t s_last_box_y = 0;                                 // 模块级静态变量 s_last_box_y，只在本文件内部使用，避免被其他文件直接修改。
static int32_t s_last_box_w = 0;                                 // 模块级静态变量 s_last_box_w，只在本文件内部使用，避免被其他文件直接修改。
static int32_t s_last_box_h = 0;                                 // 模块级静态变量 s_last_box_h，只在本文件内部使用，避免被其他文件直接修改。
static app_dock_state_t s_last_hud_state = APP_DOCK_STATE_SEARCHING; // 模块级静态变量 s_last_hud_state，只在本文件内部使用，避免被其他文件直接修改。
static uint32_t s_auth_deadline_ms = 0;                          // 模块级静态变量 s_auth_deadline_ms，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 兼容 LVGL v8/v9 获取当前活动屏幕，避免工程切换 LVGL 版本时到处改代码。
 */
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
/*
 * 统一设置普通状态标签的半透明背景、文字颜色、圆角和内边距。
 */
static void app_ui_style_label(lv_obj_t *obj)
{
    // 设置标签背景为 50% 透明，避免遮死底层摄像头画面。
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
    // 设置标签背景颜色为深灰色。
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202020), 0);
    // 设置标签文字颜色为白色。
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    // 设置标签四周内边距为 6 像素。
    lv_obj_set_style_pad_all(obj, 6, 0);
    // 设置标签圆角半径为 4 像素。
    lv_obj_set_style_radius(obj, 4, 0);
}
/*
 * 设置 HUD 覆盖层为透明且不可滚动/不可点击，避免挡住底层摄像头画面。
 */
static void app_ui_style_hud_layer(lv_obj_t *obj)
{
    // 让 HUD 容器背景完全透明。
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    // 让 HUD 容器边框完全透明。
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    // 取消可滚动标志，HUD 固定覆盖在屏幕上。
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    // LVGL v9 去掉可点击标志，避免 HUD 层拦截触摸。
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    // LVGL v8 清除可点击标志，避免 HUD 层拦截触摸。
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
/*
 * 设置中心准星线条样式，用于辅助观察无人机是否对准窗口中心。
 */
static void app_ui_style_cross_line(lv_obj_t *obj)
{
    // 设置准星线条颜色为绿色。
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x24D1A0), 0);
    // 设置准星线条为 80% 不透明。
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    // 去掉线条边框。
    lv_obj_set_style_border_width(obj, 0, 0);
    // 去掉圆角，保持线条端部为直角。
    lv_obj_set_style_radius(obj, 0, 0);
}
/*
 * 设置 AprilTag 识别框样式，默认隐藏，识别到目标后再显示。
 */
static void app_ui_style_track_box(lv_obj_t *obj)
{
    // 识别框内部透明，只显示边框。
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    // 设置识别框边框宽度为 3 像素。
    lv_obj_set_style_border_width(obj, 3, 0);
    // 设置识别框默认边框颜色为黄色。
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFD34D), 0);
    // 设置识别框边框完全不透明。
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    // 去掉圆角，保持 bbox 为直角矩形。
    lv_obj_set_style_radius(obj, 0, 0);
    // 创建后先隐藏，识别到目标时再显示。
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
/*
 * 设置稳定锁定进度条的小段样式。
 */
static void app_ui_style_lock_seg(lv_obj_t *obj)
{
    // 设置锁定条小段圆角半径为 3 像素。
    lv_obj_set_style_radius(obj, 3, 0);
    // 设置小段边框宽度为 1 像素。
    lv_obj_set_style_border_width(obj, 1, 0);
    // 设置小段默认边框颜色为灰色。
    lv_obj_set_style_border_color(obj, lv_color_hex(0x808080), 0);
    // 设置小段未点亮时的背景颜色。
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), 0);
    // 设置小段未点亮时的背景透明度。
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, 0);
}
/*
 * 设置底部/侧边提示文字样式。
 */
static void app_ui_style_hint_label(lv_obj_t *obj)
{
    // 设置提示标签背景为 50% 透明。
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
    // 设置提示标签背景颜色为深蓝黑色。
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101820), 0);
    // 设置提示标签文字颜色为浅蓝白色。
    lv_obj_set_style_text_color(obj, lv_color_hex(0xD8F3FF), 0);
    // 设置提示标签边框宽度为 1 像素。
    lv_obj_set_style_border_width(obj, 1, 0);
    // 设置提示标签边框颜色。
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2A4A58), 0);
    // 设置提示标签左右内边距。
    lv_obj_set_style_pad_hor(obj, 8, 0);
    // 设置提示标签上下内边距。
    lv_obj_set_style_pad_ver(obj, 5, 0);
    // 设置提示标签圆角半径为 4 像素。
    lv_obj_set_style_radius(obj, 4, 0);
}
/*
 * 设置鉴权通过横幅样式。
 */
static void app_ui_style_auth_label(lv_obj_t *obj)
{
    // 设置鉴权横幅背景颜色为深绿色。
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x163E31), 0);
    // 设置鉴权横幅背景不透明度。
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)192, 0);
    // 设置鉴权横幅边框宽度为 2 像素。
    lv_obj_set_style_border_width(obj, 2, 0);
    // 设置鉴权横幅边框颜色为亮绿色。
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2FE0A5), 0);
    // 设置鉴权横幅文字颜色。
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FFF7), 0);
    // 设置鉴权横幅左右内边距。
    lv_obj_set_style_pad_hor(obj, 14, 0);
    // 设置鉴权横幅上下内边距。
    lv_obj_set_style_pad_ver(obj, 10, 0);
    // 设置鉴权横幅圆角半径为 6 像素。
    lv_obj_set_style_radius(obj, 6, 0);
}

static lv_obj_t *app_ui_button_create(lv_obj_t *parent)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_button_create(parent);
#else
    return lv_btn_create(parent);
#endif
}

static void app_ui_style_capture_button(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_size(obj, 82, 42);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xE8F7FF), 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *app_ui_add_button_label(lv_obj_t *btn, const char *text)
{
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(label);
    return label;
}

static void app_ui_set_vision_text_unlocked(const char *text)
{
    if ((text != NULL) && (s_vision != NULL)) {
        lv_label_set_text(s_vision, text);
    }
}

static void app_ui_set_capture_text_unlocked(const char *text)
{
    if ((text != NULL) && (s_capture != NULL)) {
        lv_label_set_text(s_capture, text);
    }
}

static void app_ui_capture_start_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    ESP_LOGI(TAG, "CAP pressed");
    app_ui_set_capture_text_unlocked("cap: starting");
    app_ui_set_vision_text_unlocked("cap: starting");
    esp_err_t ret = app_ai_capture_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CAP start failed: %s", esp_err_to_name(ret));
        app_ui_set_capture_text_unlocked("cap: start fail");
        app_ui_set_vision_text_unlocked("cap: start fail");
    }
}

static void app_ui_capture_stop_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    ESP_LOGI(TAG, "STOP pressed");
    app_ai_capture_stop();
}

static void app_ui_capture_mode_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }

    app_ai_capture_mode_t mode = app_ai_capture_toggle_mode();
    const char *label = app_ai_capture_mode_label(mode);
    ESP_LOGI(TAG, "CAP mode pressed: %s", label);
    if (s_mode_label != NULL) {
        lv_label_set_text(s_mode_label, label);
        lv_obj_center(s_mode_label);
    }
}
/*
 * 根据接驳状态返回 HUD 主颜色，例如错误红、跟踪黄、对准蓝、就绪绿。
 */
static lv_color_t app_ui_state_color(app_dock_state_t state, bool hold_box)
{
    if (hold_box) {
        return lv_color_hex(0x7E8A93);
    }
    switch (state) {
        case APP_DOCK_STATE_WRONG_ID:
            return lv_color_hex(0xFF5D5D);
        case APP_DOCK_STATE_TRACKING:
            return lv_color_hex(0xFFD34D);
        case APP_DOCK_STATE_ALIGNED:
            return lv_color_hex(0x63D5FF);
        case APP_DOCK_STATE_READY_TO_DOCK:
            return lv_color_hex(0x31E08A);
        case APP_DOCK_STATE_SEARCHING:
        default:
            return lv_color_hex(0x7E8A93);
    }
}
/*
 * 计算摄像头画面等比例适配到屏幕后的宽高。
 */
static void app_ui_calc_fit_dims(int32_t src_w, int32_t src_h, int32_t *fit_w, int32_t *fit_h)
{
    // 参数无效时无法计算或写回适配尺寸，直接返回。
    if ((fit_w == NULL) || (fit_h == NULL) || (src_w <= 0) || (src_h <= 0)) {
        return;
    }
    float src_aspect = (float)src_w / (float)src_h;
    float dst_aspect = (float)BSP_LCD_H_RES / (float)BSP_LCD_V_RES;
    if (src_aspect > dst_aspect) {
        *fit_w = BSP_LCD_H_RES;
        *fit_h = (int32_t)((float)BSP_LCD_H_RES / src_aspect + 0.5f);
    } else {
        *fit_h = BSP_LCD_V_RES;
        *fit_w = (int32_t)((float)BSP_LCD_V_RES * src_aspect + 0.5f);
    }
}
/*
 * 根据摄像头旋转角度得到旋转后的源图尺寸。
 */
static void app_ui_get_rotated_dims(int32_t *rot_w, int32_t *rot_h)
{
    // 输出指针无效时无法写回旋转后的宽高。
    if ((rot_w == NULL) || (rot_h == NULL)) {
        return;
    }
    if ((BSP_CAMERA_ROTATION == 90) || (BSP_CAMERA_ROTATION == 270)) {
        *rot_w = HUD_SRC_H;
        *rot_h = HUD_SRC_W;
    } else {
        *rot_w = HUD_SRC_W;
        *rot_h = HUD_SRC_H;
    }
}
/*
 * 把原始摄像头坐标转换成旋转后的坐标。
 */
static void app_ui_transform_src_point(float x, float y, float *out_x, float *out_y)
{
    // 输出指针无效时无法写回旋转后的坐标。
    if ((out_x == NULL) || (out_y == NULL)) {
        return;
    }
    switch (BSP_CAMERA_ROTATION) {
        case 90:
            *out_x = (float)HUD_SRC_H - y;
            *out_y = x;
            break;
        case 180:
            *out_x = (float)HUD_SRC_W - x;
            *out_y = (float)HUD_SRC_H - y;
            break;
        case 270:
            *out_x = y;
            *out_y = (float)HUD_SRC_W - x;
            break;
        case 0:
        default:
            *out_x = x;
            *out_y = y;
            break;
    }
}
/*
 * 把坐标限制在屏幕范围内，避免 LVGL 对象跑出可视区域。
 */
static int32_t app_ui_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
/*
 * 把视觉算法中的 bbox 坐标映射到屏幕 HUD 坐标。
 */
static void app_ui_map_bbox_to_screen(const app_vision_result_t *vision,
                                      int32_t *x,
                                      int32_t *y,
                                      int32_t *w,
                                      int32_t *h)
{
    // 缺少视觉结果或输出地址时，无法完成 bbox 到屏幕坐标的映射。
    if ((vision == NULL) || (x == NULL) || (y == NULL) || (w == NULL) || (h == NULL)) {
        return;
    }

    /*
     * 视觉算法输出的是源灰度图坐标系中的 bbox。
     * 先取出四个角点，而不是只换算左上和右下，
     * 是因为摄像头画面可能被 90/180/270 度旋转。
     */
    float src_x1 = (float)vision->bbox_x;
    float src_y1 = (float)vision->bbox_y;
    float src_x2 = (float)(vision->bbox_x + vision->bbox_w);
    float src_y2 = (float)(vision->bbox_y + vision->bbox_h);
    float pts[4][2] = {
        {src_x1, src_y1},
        {src_x2, src_y1},
        {src_x2, src_y2},
        {src_x1, src_y2},
    };

    /*
     * 根据摄像头旋转角度，得到旋转后的源图宽高。
     */
    int32_t rot_w = HUD_SRC_W;
    int32_t rot_h = HUD_SRC_H;
    app_ui_get_rotated_dims(&rot_w, &rot_h);

    /*
     * 摄像头画面在屏幕上是等比例适配显示的。
     * fit_w/fit_h 是实际显示区域，off_x/off_y 是黑边偏移。
     */
    int32_t fit_w = rot_w;
    int32_t fit_h = rot_h;
    app_ui_calc_fit_dims(rot_w, rot_h, &fit_w, &fit_h);
    int32_t off_x = (BSP_LCD_H_RES - fit_w) / 2;
    int32_t off_y = (BSP_LCD_V_RES - fit_h) / 2;

    /*
     * 将 bbox 四个角点分别旋转、缩放、平移到屏幕坐标，
     * 再取 min/max 得到最终 HUD 识别框。
     */
    float min_x = 100000.0f;
    float min_y = 100000.0f;
    float max_x = -100000.0f;
    float max_y = -100000.0f;
    for (int i = 0; i < 4; i++) {
        float rx = 0.0f;
        float ry = 0.0f;
        app_ui_transform_src_point(pts[i][0], pts[i][1], &rx, &ry);
        float sx = (float)off_x + rx * (float)fit_w / (float)rot_w;
        float sy = (float)off_y + ry * (float)fit_h / (float)rot_h;
        if (sx < min_x) min_x = sx;
        if (sy < min_y) min_y = sy;
        if (sx > max_x) max_x = sx;
        if (sy > max_y) max_y = sy;
    }

    /*
     * 输出结果限制在屏幕范围内。
     * 最小宽高设为 12，避免识别框小到几乎看不见。
     */
    *x = app_ui_clamp_i32((int32_t)(min_x + 0.5f), 0, BSP_LCD_H_RES - 1);
    *y = app_ui_clamp_i32((int32_t)(min_y + 0.5f), 0, BSP_LCD_V_RES - 1);
    *w = app_ui_clamp_i32((int32_t)(max_x - min_x + 0.5f), 12, BSP_LCD_H_RES);
    *h = app_ui_clamp_i32((int32_t)(max_y - min_y + 0.5f), 12, BSP_LCD_V_RES);

    /*
     * 如果框右边或下边超出屏幕，收缩宽高而不是移动左上角。
     */
    if ((*x + *w) > BSP_LCD_H_RES) {
        *w = BSP_LCD_H_RES - *x;
    }
    if ((*y + *h) > BSP_LCD_V_RES) {
        *h = BSP_LCD_V_RES - *y;
    }
}
/*
 * 根据稳定帧数和接驳状态更新锁定进度条。
 */
static void app_ui_update_lock_bar(const app_dock_judge_result_t *dock)
{
    uint8_t filled = 0;
    lv_color_t active = lv_color_hex(0xFFD34D);
    // 有接驳结果时，根据稳定帧数和状态计算亮起数量与颜色。
    if (dock != NULL) {
        /*
         * 默认用 stable_count 填充锁定条。
         * READY 状态直接填满，强调已经满足接驳条件。
         */
        filled = (dock->stable_count > HUD_LOCK_SEG_COUNT) ? HUD_LOCK_SEG_COUNT : (uint8_t)dock->stable_count;
        if (dock->state == APP_DOCK_STATE_READY_TO_DOCK) {
            active = lv_color_hex(0x31E08A);
            filled = HUD_LOCK_SEG_COUNT;
        } else if (dock->state == APP_DOCK_STATE_ALIGNED) {
            active = lv_color_hex(0x63D5FF);
        } else if (dock->state == APP_DOCK_STATE_WRONG_ID) {
            active = lv_color_hex(0xFF5D5D);
        }
    }

    /*
     * 每个小段根据 filled 决定亮起或熄灭。
     */
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {
        // 跳过尚未创建的锁定条小段。
        if (s_lock_seg[i] == NULL) {
            continue;
        }
        if (i < filled) {
            lv_obj_set_style_bg_color(s_lock_seg[i], active, 0);
            lv_obj_set_style_bg_opa(s_lock_seg[i], LV_OPA_90, 0);
            lv_obj_set_style_border_color(s_lock_seg[i], active, 0);
        } else {
            lv_obj_set_style_bg_color(s_lock_seg[i], lv_color_hex(0x3A3A3A), 0);
            lv_obj_set_style_bg_opa(s_lock_seg[i], LV_OPA_70, 0);
            lv_obj_set_style_border_color(s_lock_seg[i], lv_color_hex(0x808080), 0);
        }
    }
}
/*
 * 在进入 READY_TO_DOCK 时短暂显示鉴权通过横幅。
 */
static void app_ui_update_auth_banner(app_dock_state_t state)
{
    uint32_t now_ms = lv_tick_get();
    if ((state == APP_DOCK_STATE_READY_TO_DOCK) &&
        (s_last_hud_state != APP_DOCK_STATE_READY_TO_DOCK)) {
        s_auth_deadline_ms = now_ms + HUD_AUTH_SHOW_MS;
    }
    // 横幅已创建且还没到截止时间时显示它。
    if ((s_auth != NULL) && (s_auth_deadline_ms != 0U) && (now_ms <= s_auth_deadline_ms)) {
        // 清除隐藏标志，让 AUTH PASSED 横幅显示出来。
        lv_obj_clear_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    // 横幅已创建但显示时间结束时，把它重新隐藏。
    } else if (s_auth != NULL) {
        // 添加隐藏标志，让横幅从屏幕上消失。
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }
    s_last_hud_state = state;
}
/*
 * 根据映射后的屏幕坐标显示或隐藏识别框，并调整位置和颜色。
 */
static void app_ui_set_track_box(bool show,
                                 int32_t x,
                                 int32_t y,
                                 int32_t w,
                                 int32_t h,
                                 lv_color_t color,
                                 lv_opa_t opa)
{
    // 识别框还没创建时无法更新。
    if (s_track_box == NULL) {
        return;
    }
    if (!show) {
        // 本帧不需要显示目标框时，添加隐藏标志。
        lv_obj_add_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(s_track_box, x, y);
    lv_obj_set_size(s_track_box, w, h);
    lv_obj_set_style_border_color(s_track_box, color, 0);
    lv_obj_set_style_border_opa(s_track_box, opa, 0);
    // 清除隐藏标志，显示更新后的识别框。
    lv_obj_clear_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
}
/*
 * 创建主 UI 控件、HUD 层和状态标签。
 */
bool app_ui_create(void)
{
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    if (!bsp_display_lock(0)) {
        return false;
    }
    lv_obj_t *scr = app_get_active_screen();
    // HUD 覆盖层只创建一次，尺寸铺满整块屏幕。
    if (s_hud_layer == NULL) {
        s_hud_layer = lv_obj_create(scr);
        app_ui_style_hud_layer(s_hud_layer);
        lv_obj_set_size(s_hud_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_align(s_hud_layer, LV_ALIGN_CENTER, 0, 0);// 将 HUD 层对齐到屏幕中心。
    }
    // 识别框创建在 HUD 层上，后续只更新位置、大小和颜色。
    if (s_track_box == NULL) {
        s_track_box = lv_obj_create(s_hud_layer);
        app_ui_style_track_box(s_track_box);
    }
    // 创建水平中心准星。
    if (s_cross_h == NULL) {
        s_cross_h = lv_obj_create(s_hud_layer);
        app_ui_style_cross_line(s_cross_h);
        lv_obj_set_size(s_cross_h, 48, 2);
        lv_obj_align(s_cross_h, LV_ALIGN_CENTER, 0, 0);
    }
    // 创建垂直中心准星。
    if (s_cross_v == NULL) {
        s_cross_v = lv_obj_create(s_hud_layer);
        app_ui_style_cross_line(s_cross_v);
        lv_obj_set_size(s_cross_v, 2, 48);
        lv_obj_align(s_cross_v, LV_ALIGN_CENTER, 0, 0);
    }
    const int32_t seg_w = 30;
    const int32_t seg_h = 14;
    const int32_t seg_gap = 8;
    const int32_t total_w = (HUD_LOCK_SEG_COUNT * seg_w) + ((HUD_LOCK_SEG_COUNT - 1) * seg_gap);
    const int32_t start_x = (BSP_LCD_H_RES - total_w) / 2;
    const int32_t y = 22;
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {
        // 逐个创建顶部锁定条小段。
        if (s_lock_seg[i] == NULL) {
            s_lock_seg[i] = lv_obj_create(scr);
            app_ui_style_lock_seg(s_lock_seg[i]);
            lv_obj_set_size(s_lock_seg[i], seg_w, seg_h);
            lv_obj_set_pos(s_lock_seg[i], start_x + i * (seg_w + seg_gap), y);
        }
    }
    // 创建鉴权通过横幅，默认隐藏。
    if (s_auth == NULL) {
        s_auth = lv_label_create(scr);
        app_ui_style_auth_label(s_auth);
        lv_label_set_text(s_auth, "AUTH PASSED");
        lv_obj_align(s_auth, LV_ALIGN_CENTER, 0, -32);
        // 添加隐藏标志，等 READY_TO_DOCK 时再显示。
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }
    // 创建左上角主状态标签。
    if (s_status == NULL) {
        s_status = lv_label_create(scr);
        app_ui_style_label(s_status);
        lv_label_set_text(s_status, "dock: init");
        lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 8, 8);
    }
    // 创建右上角视觉识别状态标签。
    if (s_vision == NULL) {
        s_vision = lv_label_create(scr);
        app_ui_style_label(s_vision);
        lv_label_set_text(s_vision, "vision: init");
        lv_obj_align(s_vision, LV_ALIGN_TOP_RIGHT, -8, 8);
    }
    // 创建底部接驳调试信息标签。
    if (s_dock == NULL) {
        s_dock = lv_label_create(scr);
        app_ui_style_label(s_dock);
        lv_label_set_text(s_dock, "dock dbg: init");
        lv_obj_set_width(s_dock, BSP_LCD_H_RES - 220);
        lv_obj_set_style_text_align(s_dock, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    // 创建顶部提示标签。
    if (s_hint == NULL) {
        s_hint = lv_label_create(scr);
        app_ui_style_hint_label(s_hint);
        lv_label_set_text(s_hint, "cloud dispatch enabled / no touch control");
        lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 46);
    }
    if (s_cap_btn == NULL) {
        s_cap_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_cap_btn, lv_color_hex(0x177A58));
        app_ui_add_button_label(s_cap_btn, "CAP");
        lv_obj_align(s_cap_btn, LV_ALIGN_RIGHT_MID, -14, 68);
        lv_obj_add_event_cb(s_cap_btn, app_ui_capture_start_event_cb, LV_EVENT_PRESSED, NULL);
    }
    if (s_stop_btn == NULL) {
        s_stop_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_stop_btn, lv_color_hex(0x8A3030));
        app_ui_add_button_label(s_stop_btn, "STOP");
        lv_obj_align(s_stop_btn, LV_ALIGN_RIGHT_MID, -14, 118);
        lv_obj_add_event_cb(s_stop_btn, app_ui_capture_stop_event_cb, LV_EVENT_PRESSED, NULL);
    }
    if (s_mode_btn == NULL) {
        s_mode_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_mode_btn, lv_color_hex(0x2F5D88));
        s_mode_label = app_ui_add_button_label(s_mode_btn,
                                               app_ai_capture_mode_label(app_ai_capture_get_mode()));
        lv_obj_align(s_mode_btn, LV_ALIGN_RIGHT_MID, -14, 18);
        lv_obj_add_event_cb(s_mode_btn, app_ui_capture_mode_event_cb, LV_EVENT_PRESSED, NULL);
    }
    if (s_capture == NULL) {
        s_capture = lv_label_create(scr);
        app_ui_style_hint_label(s_capture);
        lv_label_set_text(s_capture, "cap:DRONE off #0");
        lv_obj_set_width(s_capture, 132);
        lv_obj_set_style_text_align(s_capture, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_capture, LV_ALIGN_RIGHT_MID, -14, 166);
    }
    if (s_status) lv_obj_move_foreground(s_status);
    if (s_vision) lv_obj_move_foreground(s_vision);
    if (s_coord) lv_obj_move_foreground(s_coord);
    if (s_dock) lv_obj_move_foreground(s_dock);
    if (s_hint) lv_obj_move_foreground(s_hint);
    if (s_auth) lv_obj_move_foreground(s_auth);
    if (s_mode_btn) lv_obj_move_foreground(s_mode_btn);
    if (s_cap_btn) lv_obj_move_foreground(s_cap_btn);
    if (s_stop_btn) lv_obj_move_foreground(s_stop_btn);
    if (s_capture) lv_obj_move_foreground(s_capture);
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
    return true;
}
/*
 * 线程安全更新主状态文本。
 */
void app_ui_set_status(const char *text)
{
    // 没有文本或状态标签尚未创建时不更新。
    if ((text == NULL) || (s_status == NULL)) {
        return;
    }
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_status, text);
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
}
/*
 * 线程安全更新视觉识别文本。
 */
void app_ui_set_vision_text(const char *text)
{
    // 没有文本或视觉标签尚未创建时不更新。
    if ((text == NULL) || (s_vision == NULL)) {
        return;
    }
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_vision, text);
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
}

void app_ui_set_capture_text(const char *text)
{
    if ((text == NULL) || (s_capture == NULL)) {
        return;
    }
    if (!bsp_display_lock(0)) {
        return;
    }
    app_ui_set_capture_text_unlocked(text);
    bsp_display_unlock();
}
/*
 * 线程安全更新接驳调试文本。
 */
void app_ui_set_dock_text(const char *text)
{
    // 没有文本或接驳调试标签尚未创建时不更新。
    if ((text == NULL) || (s_dock == NULL)) {
        return;
    }
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_dock, text);
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
}
/*
 * 根据视觉结果和接驳判定结果刷新识别框、准星、锁定条和鉴权提示。
 */
void app_ui_update_hud(const app_vision_result_t *vision,
                       const app_dock_judge_result_t *dock)
{
    // 缺少接驳结果或核心 HUD 控件尚未创建时无法刷新。
    if ((dock == NULL) || (s_hud_layer == NULL) || (s_track_box == NULL)) {
        return;
    }
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    if (!bsp_display_lock(0)) {
        return;
    }
    bool hold_box = false;
    bool show_box = false;
    int32_t box_x = 0;
    int32_t box_y = 0;
    int32_t box_w = 0;
    int32_t box_h = 0;

    /*
     * 优先使用当前有效视觉结果。
     * 如果当前帧有效，就重新计算识别框并保存为 last_box。
     */
    // 当前视觉结果有效时，用它计算并缓存目标框。
    if ((vision != NULL) && vision->valid) {
        app_ui_map_bbox_to_screen(vision, &box_x, &box_y, &box_w, &box_h);
        s_have_last_box = true;
        s_last_box_x = box_x;
        s_last_box_y = box_y;
        s_last_box_w = box_w;
        s_last_box_h = box_h;
        show_box = true;
    } else if (s_have_last_box && (dock->invalid_hold_count > 0U) &&
               (dock->state != APP_DOCK_STATE_SEARCHING)) {
        /*
         * 当前帧无效但接驳判定仍处于 hold 窗口时，
         * 继续显示上一帧识别框，并降低透明度，提示这是保持状态而不是新识别。
         */
        box_x = s_last_box_x;
        box_y = s_last_box_y;
        box_w = s_last_box_w;
        box_h = s_last_box_h;
        show_box = true;
        hold_box = true;
    } else {
        /*
         * 已经没有可保持的目标框。
         */
        s_have_last_box = false;
    }

    /*
     * 识别框颜色跟随接驳状态变化：
     * wrong_id 红色，tracking 黄色，aligned 蓝色，ready 绿色，hold 灰色。
     */
    lv_color_t box_color = app_ui_state_color(dock->state, hold_box);
    lv_opa_t box_opa = hold_box ? LV_OPA_50 : LV_OPA_COVER;
    app_ui_set_track_box(show_box, box_x, box_y, box_w, box_h, box_color, box_opa);

    // 准星两个控件都创建后，再跟随接驳状态更新颜色。
    if ((s_cross_h != NULL) && (s_cross_v != NULL)) {
        /*
         * 中心准星颜色也跟随接驳状态，方便用户不用看文字也能判断当前阶段。
         */
        lv_color_t cross_color = app_ui_state_color(dock->state, false);
        lv_obj_set_style_bg_color(s_cross_h, cross_color, 0);
        lv_obj_set_style_bg_color(s_cross_v, cross_color, 0);
    }

    /*
     * 更新锁定条和 AUTH PASSED 横幅。
     */
    app_ui_update_lock_bar(dock);
    app_ui_update_auth_banner(dock->state);
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
}
