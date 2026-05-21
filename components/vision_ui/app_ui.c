/* 实现说明：持久化 LVGL 控件统一由本模块创建和维护。 */
/*
 * app_ui.c - LVGL 用户界面和视觉 HUD 覆盖层模块
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

#include "app_ui.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "app_ai_capture.h"
#ifndef BSP_CAMERA_ROTATION
#define BSP_CAMERA_ROTATION 0
#endif

LV_FONT_DECLARE(font_loading_cn)
LV_FONT_DECLARE(font_title_en)
LV_IMAGE_DECLARE(logo);

/* -------------------------------------------------------------------------- */
/* HUD 布局                                                                  */
/* -------------------------------------------------------------------------- */

#define HUD_SRC_W               320
#define HUD_SRC_H               240
#define HUD_LOCK_SEG_COUNT      2
#define HUD_AUTH_SHOW_MS        1200
#define UI_LOCK_SHORT_MS        30
#define UI_LOCK_BOOT_MS         300
#define UI_PROJECT_NAME         "基于ESP32-P4双芯协同的低空接驳微港与端侧鉴权闭环系统"
#define UI_CONTEST_NAME         "第九届（2026）全国大学生嵌入式芯片与系统设计竞赛"
#define UI_TEAM_NAME            "地线引力队"
#define UI_CLOCK_VALID_EPOCH    1704067200LL
static const char *TAG = "app_ui";

/* -------------------------------------------------------------------------- */
/* LVGL 对象状态                                                           */
/* -------------------------------------------------------------------------- */

static lv_obj_t *s_status = NULL;
static lv_obj_t *s_coord = NULL;
static lv_obj_t *s_vision = NULL;
static lv_obj_t *s_dock = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_hud_layer = NULL;
static lv_obj_t *s_track_box = NULL;
static lv_obj_t *s_cross_h = NULL;
static lv_obj_t *s_cross_v = NULL;
static lv_obj_t *s_lock_seg[HUD_LOCK_SEG_COUNT] = {0};
static lv_obj_t *s_auth = NULL;
static lv_obj_t *s_cap_btn = NULL;
static lv_obj_t *s_stop_btn = NULL;
static lv_obj_t *s_mode_btn = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_capture = NULL;
static lv_obj_t *s_loading_layer = NULL;
static lv_obj_t *s_loading_detail = NULL;
static lv_obj_t *s_loading_bar = NULL;
static lv_obj_t *s_main_layer = NULL;
static lv_obj_t *s_main_wifi_ind = NULL;
static lv_obj_t *s_main_mqtt_ind = NULL;
static lv_obj_t *s_main_ch32_ind = NULL;
static lv_obj_t *s_main_task_label = NULL;
static lv_obj_t *s_main_task_card_label = NULL;
static lv_obj_t *s_main_conn_label = NULL;
static lv_obj_t *s_main_dock_label = NULL;
static lv_obj_t *s_main_clock_time = NULL;
static lv_obj_t *s_main_clock_note = NULL;
static lv_obj_t *s_main_weather_label = NULL;
static lv_obj_t *s_main_pickup_btn = NULL;
static lv_timer_t *s_main_clock_timer = NULL;
static app_ui_pickup_cb_t s_pickup_cb = NULL;
static bool s_have_last_box = false;
static int32_t s_last_box_x = 0;
static int32_t s_last_box_y = 0;
static int32_t s_last_box_w = 0;
static int32_t s_last_box_h = 0;
static app_dock_state_t s_last_hud_state = APP_DOCK_STATE_SEARCHING;
static uint32_t s_auth_deadline_ms = 0;

/* -------------------------------------------------------------------------- */
/* LVGL 兼容和样式                                               */
/* -------------------------------------------------------------------------- */

/* 兼容 LVGL 8/9 获取当前活动屏幕对象。 */
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
/* 给普通 HUD 文本应用统一的半透明样式。 */
static void app_ui_style_label(lv_obj_t *obj)
{

    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);

    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202020), 0);

    lv_obj_set_style_text_color(obj, lv_color_white(), 0);

    lv_obj_set_style_pad_all(obj, 6, 0);

    lv_obj_set_style_radius(obj, 4, 0);
}
/* 设置 HUD 根层样式，让其透明且不拦截交互。 */
static void app_ui_style_hud_layer(lv_obj_t *obj)
{

    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
/* 设置中心准星线条的颜色、宽度和圆角。 */
static void app_ui_style_cross_line(lv_obj_t *obj)
{

    lv_obj_set_style_bg_color(obj, lv_color_hex(0x24D1A0), 0);

    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);

    lv_obj_set_style_border_width(obj, 0, 0);

    lv_obj_set_style_radius(obj, 0, 0);
}
/* 设置视觉跟踪框的边框样式。 */
static void app_ui_style_track_box(lv_obj_t *obj)
{

    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(obj, 3, 0);

    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFD34D), 0);

    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(obj, 0, 0);

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
/* 设置锁定进度条分段的默认样式。 */
static void app_ui_style_lock_seg(lv_obj_t *obj)
{

    lv_obj_set_style_radius(obj, 3, 0);

    lv_obj_set_style_border_width(obj, 1, 0);

    lv_obj_set_style_border_color(obj, lv_color_hex(0x808080), 0);

    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), 0);

    lv_obj_set_style_bg_opa(obj, LV_OPA_70, 0);
}
/* 设置底部提示文本样式。 */
static void app_ui_style_hint_label(lv_obj_t *obj)
{

    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);

    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101820), 0);

    lv_obj_set_style_text_color(obj, lv_color_hex(0xD8F3FF), 0);

    lv_obj_set_style_border_width(obj, 1, 0);

    lv_obj_set_style_border_color(obj, lv_color_hex(0x2A4A58), 0);

    lv_obj_set_style_pad_hor(obj, 8, 0);

    lv_obj_set_style_pad_ver(obj, 5, 0);

    lv_obj_set_style_radius(obj, 4, 0);
}
/* 设置 AUTH PASSED 横幅文本样式。 */
static void app_ui_style_auth_label(lv_obj_t *obj)
{

    lv_obj_set_style_bg_color(obj, lv_color_hex(0x163E31), 0);

    lv_obj_set_style_bg_opa(obj, (lv_opa_t)192, 0);

    lv_obj_set_style_border_width(obj, 2, 0);

    lv_obj_set_style_border_color(obj, lv_color_hex(0x2FE0A5), 0);

    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FFF7), 0);

    lv_obj_set_style_pad_hor(obj, 14, 0);

    lv_obj_set_style_pad_ver(obj, 10, 0);

    lv_obj_set_style_radius(obj, 6, 0);
}

/* 设置启动加载层背景样式。 */
static void app_ui_style_loading_layer(lv_obj_t *obj)
{
    lv_obj_set_size(obj, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xF4F7F9), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/* 设置启动加载页详情文本样式。 */
static void app_ui_style_loading_detail(lv_obj_t *obj)
{
    lv_obj_set_width(obj, 420);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x263544), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
}

/* 创建不抢眼的浅色面板。 */
static lv_obj_t *app_ui_create_soft_panel(lv_obj_t *parent,
    int32_t w,
    int32_t h,
    int32_t radius)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFB), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xD7E2E8), 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

/* 状态值面板标题。 */
static lv_obj_t *app_ui_create_panel_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_hex(0x60717D), 0);
    lv_obj_set_style_text_font(label, &font_loading_cn, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 12);
    return label;
}

/* 状态值面板主文本。 */
static lv_obj_t *app_ui_create_panel_value(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_obj_get_width(parent) - 32);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1F6F7A), 0);
    lv_obj_set_style_text_font(label, &font_loading_cn, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 16, -14);
    return label;
}

/* 在已持有 LVGL 锁时更新加载页详情文本。 */
static void app_ui_set_loading_text_unlocked(const char *text)
{
    (void)text;
    if (s_loading_detail != NULL)
    {
        lv_label_set_text(s_loading_detail, "系统正在启动");
    }
}

/* 创建一个基础按钮并应用项目通用尺寸和样式。 */
static lv_obj_t *app_ui_button_create(lv_obj_t *parent)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_button_create(parent);
#else
    return lv_btn_create(parent);
#endif
}

/* 按当前模式颜色刷新抓图按钮样式。 */
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

/* 给按钮添加居中的文字标签。 */
static lv_obj_t *app_ui_add_button_label(lv_obj_t *btn, const char *text)
{
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(label);
    return label;
}

/* 在已持有 LVGL 锁时安全更新标签文本。 */
static void app_ui_set_label_text_unlocked(lv_obj_t *label, const char *text)
{
    if ((label != NULL) && (text != NULL))
    {
        const char *old_text = lv_label_get_text(label);
        if ((old_text != NULL) && (strcmp(old_text, text) == 0))
        {
            return;
        }
        lv_label_set_text(label, text);
    }
}

/* 在已持有 LVGL 锁时更新视觉状态文本。 */
static void app_ui_set_vision_text_unlocked(const char *text)
{
    app_ui_set_label_text_unlocked(s_vision, text);
}

/* 在已持有 LVGL 锁时更新抓图状态文本。 */
static void app_ui_set_capture_text_unlocked(const char *text)
{
    app_ui_set_label_text_unlocked(s_capture, text);
}

/* -------------------------------------------------------------------------- */
/* 抓拍按钮回调                                                    */
/* -------------------------------------------------------------------------- */

/* 抓图开始按钮事件回调，启动 AI 抓图流程。 */
static void app_ui_capture_start_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }
    ESP_LOGD(TAG, "CAP pressed");
    app_ui_set_capture_text_unlocked("cap: starting");
    app_ui_set_vision_text_unlocked("cap: starting");
    esp_err_t ret = app_ai_capture_start();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "CAP start failed: %s", esp_err_to_name(ret));
        app_ui_set_capture_text_unlocked("cap: start fail");
        app_ui_set_vision_text_unlocked("cap: start fail");
    }
}

/* 抓图停止按钮事件回调，停止 AI 抓图流程。 */
static void app_ui_capture_stop_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }
    ESP_LOGD(TAG, "STOP pressed");
    app_ai_capture_stop();
}

/* 抓图模式按钮事件回调，在单张/连续模式间切换。 */
static void app_ui_capture_mode_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }

    app_ai_capture_mode_t mode = app_ai_capture_toggle_mode();
    const char *label = app_ai_capture_mode_label(mode);
    ESP_LOGD(TAG, "CAP mode pressed: %s", label);
    if (s_mode_label != NULL)
    {
        lv_label_set_text(s_mode_label, label);
        lv_obj_center(s_mode_label);
    }
}

/* -------------------------------------------------------------------------- */
/* HUD 几何和状态映射                                              */
/* -------------------------------------------------------------------------- */

/* 根据接驳状态选择跟踪框和锁定条颜色。 */
static lv_color_t app_ui_state_color(app_dock_state_t state, bool hold_box)
{
    if (hold_box)
    {
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
/* 计算摄像头画面等比适配屏幕后的显示尺寸。 */
static void app_ui_calc_fit_dims(int32_t src_w, int32_t src_h, int32_t *fit_w, int32_t *fit_h)
{

    if ((fit_w == NULL) || (fit_h == NULL) || (src_w <= 0) || (src_h <= 0))
    {
        return;
    }
    float src_aspect = (float)src_w / (float)src_h;
    float dst_aspect = (float)BSP_LCD_H_RES / (float)BSP_LCD_V_RES;
    if (src_aspect > dst_aspect)
    {
        *fit_w = BSP_LCD_H_RES;
        *fit_h = (int32_t)((float)BSP_LCD_H_RES / src_aspect + 0.5f);
    }
    else
    {
        *fit_h = BSP_LCD_V_RES;
        *fit_w = (int32_t)((float)BSP_LCD_V_RES * src_aspect + 0.5f);
    }
}
/* 根据 BSP 旋转角度计算预览画面旋转后的尺寸。 */
static void app_ui_get_rotated_dims(int32_t src_w, int32_t src_h, int32_t *rot_w, int32_t *rot_h)
{

    if ((rot_w == NULL) || (rot_h == NULL) || (src_w <= 0) || (src_h <= 0))
    {
        return;
    }
    if ((BSP_CAMERA_ROTATION == 90) || (BSP_CAMERA_ROTATION == 270))
    {
        *rot_w = src_h;
        *rot_h = src_w;
    }
    else
    {
        *rot_w = src_w;
        *rot_h = src_h;
    }
}
/* 把视觉坐标系中的一个点映射到屏幕预览坐标系。 */
static void app_ui_transform_src_point(float x,
    float y,
    int32_t src_w,
    int32_t src_h,
    float *out_x,
    float *out_y)
{

    if ((out_x == NULL) || (out_y == NULL) || (src_w <= 0) || (src_h <= 0))
    {
        return;
    }
    switch (BSP_CAMERA_ROTATION) {
    case 90:
        *out_x = (float)src_h - y;
        *out_y = x;
        break;
    case 180:
        *out_x = (float)src_w - x;
        *out_y = (float)src_h - y;
        break;
    case 270:
        *out_x = y;
        *out_y = (float)src_w - x;
        break;
    case 0:
    default:
        *out_x = x;
        *out_y = y;
        break;
    }
}
/* 将坐标值裁剪到指定范围内。 */
static int32_t app_ui_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
/* 将 AprilTag 外接框从灰度裁剪图坐标映射到屏幕坐标。 */
static void app_ui_map_bbox_to_screen(const app_vision_result_t *vision,
    int32_t *x,
    int32_t *y,
    int32_t *w,
    int32_t *h)
{

    if ((vision == NULL) || (x == NULL) || (y == NULL) || (w == NULL) || (h == NULL))
    {
        return;
    }
    int32_t gray_w = (vision->gray_width > 0U) ? (int32_t)vision->gray_width : HUD_SRC_W;
    int32_t gray_h = (vision->gray_height > 0U) ? (int32_t)vision->gray_height : HUD_SRC_H;
    int32_t src_w = (vision->src_width > 0U) ? (int32_t)vision->src_width : gray_w;
    int32_t src_h = (vision->src_height > 0U) ? (int32_t)vision->src_height : gray_h;
    int32_t crop_x = (int32_t)vision->crop_x;
    int32_t crop_y = (int32_t)vision->crop_y;
    int32_t crop_w = (vision->crop_w > 0U) ? (int32_t)vision->crop_w : gray_w;
    int32_t crop_h = (vision->crop_h > 0U) ? (int32_t)vision->crop_h : gray_h;

    float gray_x1 = (float)app_ui_clamp_i32(vision->bbox_x, 0, gray_w);
    float gray_y1 = (float)app_ui_clamp_i32(vision->bbox_y, 0, gray_h);
    float gray_x2 = (float)app_ui_clamp_i32(vision->bbox_x + vision->bbox_w, 0, gray_w);
    float gray_y2 = (float)app_ui_clamp_i32(vision->bbox_y + vision->bbox_h, 0, gray_h);
    float src_x1 = (float)crop_x + gray_x1 * (float)crop_w / (float)gray_w;
    float src_y1 = (float)crop_y + gray_y1 * (float)crop_h / (float)gray_h;
    float src_x2 = (float)crop_x + gray_x2 * (float)crop_w / (float)gray_w;
    float src_y2 = (float)crop_y + gray_y2 * (float)crop_h / (float)gray_h;
    float pts[4][2] = {
        {src_x1, src_y1},
        {src_x2, src_y1},
        {src_x2, src_y2},
        {src_x1, src_y2},
    };
    int32_t rot_w = src_w;
    int32_t rot_h = src_h;
    app_ui_get_rotated_dims(src_w, src_h, &rot_w, &rot_h);
    int32_t fit_w = rot_w;
    int32_t fit_h = rot_h;
    app_ui_calc_fit_dims(rot_w, rot_h, &fit_w, &fit_h);
    int32_t off_x = (BSP_LCD_H_RES - fit_w) / 2;
    int32_t off_y = (BSP_LCD_V_RES - fit_h) / 2;
    float min_x = 100000.0f;
    float min_y = 100000.0f;
    float max_x = -100000.0f;
    float max_y = -100000.0f;
    for (int i = 0; i < 4; i++) {
        float rx = 0.0f;
        float ry = 0.0f;
        app_ui_transform_src_point(pts[i][0], pts[i][1], src_w, src_h, &rx, &ry);
        float sx = (float)off_x + rx * (float)fit_w / (float)rot_w;
        float sy = (float)off_y + ry * (float)fit_h / (float)rot_h;
        if (sx < min_x) min_x = sx;
        if (sy < min_y) min_y = sy;
        if (sx > max_x) max_x = sx;
        if (sy > max_y) max_y = sy;
    }
    *x = app_ui_clamp_i32((int32_t)(min_x + 0.5f), 0, BSP_LCD_H_RES - 1);
    *y = app_ui_clamp_i32((int32_t)(min_y + 0.5f), 0, BSP_LCD_V_RES - 1);
    *w = app_ui_clamp_i32((int32_t)(max_x - min_x + 0.5f), 12, BSP_LCD_H_RES);
    *h = app_ui_clamp_i32((int32_t)(max_y - min_y + 0.5f), 12, BSP_LCD_V_RES);
    if ((*x + *w) > BSP_LCD_H_RES)
    {
        *w = BSP_LCD_H_RES - *x;
    }
    if ((*y + *h) > BSP_LCD_V_RES)
    {
        *h = BSP_LCD_V_RES - *y;
    }
}
/* 按 hover_score 和状态刷新底部锁定进度条。 */
static void app_ui_update_lock_bar(const app_dock_judge_result_t *dock)
{
    uint8_t filled = 0;
    lv_color_t active = lv_color_hex(0xFFD34D);

    if (dock != NULL)
    {
        filled = (dock->stable_count > HUD_LOCK_SEG_COUNT) ? HUD_LOCK_SEG_COUNT : (uint8_t)dock->stable_count;
        if (dock->state == APP_DOCK_STATE_READY_TO_DOCK)
        {
            active = lv_color_hex(0x31E08A);
            filled = HUD_LOCK_SEG_COUNT;
        }
        else if (dock->state == APP_DOCK_STATE_ALIGNED)
        {
            active = lv_color_hex(0x63D5FF);
        }
        else if (dock->state == APP_DOCK_STATE_WRONG_ID)
        {
            active = lv_color_hex(0xFF5D5D);
        }
    }
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {

        if (s_lock_seg[i] == NULL)
        {
            continue;
        }
        if (i < filled)
        {
            lv_obj_set_style_bg_color(s_lock_seg[i], active, 0);
            lv_obj_set_style_bg_opa(s_lock_seg[i], LV_OPA_90, 0);
            lv_obj_set_style_border_color(s_lock_seg[i], active, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(s_lock_seg[i], lv_color_hex(0x3A3A3A), 0);
            lv_obj_set_style_bg_opa(s_lock_seg[i], LV_OPA_70, 0);
            lv_obj_set_style_border_color(s_lock_seg[i], lv_color_hex(0x808080), 0);
        }
    }
}
/* 根据 ready 状态显示或隐藏 AUTH PASSED 横幅。 */
static void app_ui_update_auth_banner(app_dock_state_t state)
{
    uint32_t now_ms = lv_tick_get();
    if ((state == APP_DOCK_STATE_READY_TO_DOCK) &&
        (s_last_hud_state != APP_DOCK_STATE_READY_TO_DOCK))
    {
        s_auth_deadline_ms = now_ms + HUD_AUTH_SHOW_MS;
    }

    if ((s_auth != NULL) && (s_auth_deadline_ms != 0U) && (now_ms <= s_auth_deadline_ms))
    {

        lv_obj_clear_flag(s_auth, LV_OBJ_FLAG_HIDDEN);

    }
    else if (s_auth != NULL)
    {

        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }
    s_last_hud_state = state;
}
/* 显示、隐藏或移动视觉跟踪框。 */
static void app_ui_set_track_box(bool show,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    lv_color_t color,
    lv_opa_t opa)
{

    if (s_track_box == NULL)
    {
        return;
    }
    if (!show)
    {

        lv_obj_add_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(s_track_box, x, y);
    lv_obj_set_size(s_track_box, w, h);
    lv_obj_set_style_border_color(s_track_box, color, 0);
    lv_obj_set_style_border_opa(s_track_box, opa, 0);

    lv_obj_clear_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

/* 创建主 HUD、文本标签、按钮、准星和锁定条。 */
bool app_ui_create(void)
{

    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }
    lv_obj_t *scr = app_get_active_screen();

    if (s_hud_layer == NULL)
    {
        s_hud_layer = lv_obj_create(scr);
        app_ui_style_hud_layer(s_hud_layer);
        lv_obj_set_size(s_hud_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_align(s_hud_layer, LV_ALIGN_CENTER, 0, 0);
    }

    if (s_track_box == NULL)
    {
        s_track_box = lv_obj_create(s_hud_layer);
        app_ui_style_track_box(s_track_box);
    }

    if (s_cross_h == NULL)
    {
        s_cross_h = lv_obj_create(s_hud_layer);
        app_ui_style_cross_line(s_cross_h);
        lv_obj_set_size(s_cross_h, 48, 2);
        lv_obj_align(s_cross_h, LV_ALIGN_CENTER, 0, 0);
    }

    if (s_cross_v == NULL)
    {
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

        if (s_lock_seg[i] == NULL)
        {
            s_lock_seg[i] = lv_obj_create(scr);
            app_ui_style_lock_seg(s_lock_seg[i]);
            lv_obj_set_size(s_lock_seg[i], seg_w, seg_h);
            lv_obj_set_pos(s_lock_seg[i], start_x + i * (seg_w + seg_gap), y);
        }
    }

    if (s_auth == NULL)
    {
        s_auth = lv_label_create(scr);
        app_ui_style_auth_label(s_auth);
        lv_label_set_text(s_auth, "AUTH PASSED");
        lv_obj_align(s_auth, LV_ALIGN_CENTER, 0, -32);

        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_status == NULL)
    {
        s_status = lv_label_create(scr);
        app_ui_style_label(s_status);
        lv_label_set_text(s_status, "dock: init");
        lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 8, 8);
    }

    if (s_vision == NULL)
    {
        s_vision = lv_label_create(scr);
        app_ui_style_label(s_vision);
        lv_label_set_text(s_vision, "vision: init");
        lv_obj_align(s_vision, LV_ALIGN_TOP_RIGHT, -8, 8);
    }

    if (s_dock == NULL)
    {
        s_dock = lv_label_create(scr);
        app_ui_style_label(s_dock);
        lv_label_set_text(s_dock, "dock dbg: init");
        lv_obj_set_width(s_dock, BSP_LCD_H_RES - 220);
        lv_obj_set_style_text_align(s_dock, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    if (s_hint == NULL)
    {
        s_hint = lv_label_create(scr);
        app_ui_style_hint_label(s_hint);
        lv_label_set_text(s_hint, "cloud dispatch enabled / no touch control");
        lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 46);
    }
    if (s_cap_btn == NULL)
    {
        s_cap_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_cap_btn, lv_color_hex(0x177A58));
        app_ui_add_button_label(s_cap_btn, "CAP");
        lv_obj_align(s_cap_btn, LV_ALIGN_RIGHT_MID, -14, 68);
        lv_obj_add_event_cb(s_cap_btn, app_ui_capture_start_event_cb, LV_EVENT_PRESSED, NULL);
    }
    if (s_stop_btn == NULL)
    {
        s_stop_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_stop_btn, lv_color_hex(0x8A3030));
        app_ui_add_button_label(s_stop_btn, "STOP");
        lv_obj_align(s_stop_btn, LV_ALIGN_RIGHT_MID, -14, 118);
        lv_obj_add_event_cb(s_stop_btn, app_ui_capture_stop_event_cb, LV_EVENT_PRESSED, NULL);
    }
    if (s_mode_btn == NULL)
    {
        s_mode_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_mode_btn, lv_color_hex(0x2F5D88));
        s_mode_label = app_ui_add_button_label(s_mode_btn,
            app_ai_capture_mode_label(app_ai_capture_get_mode()));
        lv_obj_align(s_mode_btn, LV_ALIGN_RIGHT_MID, -14, 18);
        lv_obj_add_event_cb(s_mode_btn, app_ui_capture_mode_event_cb, LV_EVENT_PRESSED, NULL);
    }
    if (s_capture == NULL)
    {
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

    bsp_display_unlock();
    return true;
}

/* 创建并显示启动加载层，用于遮住慢启动阶段的白屏。 */
bool app_ui_show_loading(const char *text)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }

    lv_obj_t *scr = app_get_active_screen();
    if (s_loading_layer == NULL)
    {
        s_loading_layer = lv_obj_create(scr);
        app_ui_style_loading_layer(s_loading_layer);
        lv_obj_align(s_loading_layer, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *logo_panel = lv_obj_create(s_loading_layer);
        lv_obj_set_size(logo_panel, 304, 190);
        lv_obj_set_style_bg_color(logo_panel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(logo_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(logo_panel, 0, 0);
        lv_obj_set_style_radius(logo_panel, 0, 0);
        lv_obj_set_style_pad_all(logo_panel, 0, 0);
        lv_obj_clear_flag(logo_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(logo_panel, LV_ALIGN_TOP_MID, 0, 16);

        lv_obj_t *logo_img = lv_image_create(logo_panel);
        lv_image_set_src(logo_img, &logo);
        lv_image_set_scale(logo_img, 316);
        lv_obj_center(logo_img);

        s_loading_detail = lv_label_create(s_loading_layer);
        app_ui_style_loading_detail(s_loading_detail);
        lv_obj_set_style_text_color(s_loading_detail, lv_color_hex(0x263544), 0);
        lv_label_set_text(s_loading_detail, "系统正在启动");
        lv_obj_align(s_loading_detail, LV_ALIGN_CENTER, 0, -18);

        s_loading_bar = lv_bar_create(s_loading_layer);
        lv_obj_set_size(s_loading_bar, 360, 6);
        lv_obj_set_style_bg_color(s_loading_bar, lv_color_hex(0xD6E0E6), 0);
        lv_obj_set_style_bg_opa(s_loading_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_loading_bar, 3, 0);
        lv_obj_set_style_bg_color(s_loading_bar, lv_color_hex(0x2F7E8A), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_loading_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_loading_bar, 3, LV_PART_INDICATOR);
        lv_bar_set_range(s_loading_bar, 0, 100);
        lv_bar_set_value(s_loading_bar, 0, LV_ANIM_OFF);
        lv_obj_align(s_loading_bar, LV_ALIGN_CENTER, 0, 28);

        lv_obj_t *team = lv_label_create(s_loading_layer);
        lv_obj_set_style_text_color(team, lv_color_hex(0x425564), 0);
        lv_obj_set_style_text_align(team, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(team, &font_loading_cn, 0);
        lv_label_set_text(team, UI_TEAM_NAME);
        lv_obj_align(team, LV_ALIGN_BOTTOM_MID, 0, -36);

        lv_obj_t *contest = lv_label_create(s_loading_layer);
        lv_obj_set_style_text_color(contest, lv_color_hex(0x6E8794), 0);
        lv_obj_set_style_text_align(contest, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(contest, &font_loading_cn, 0);
        lv_label_set_text(contest, UI_CONTEST_NAME);
        lv_obj_align(contest, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    else
    {
        lv_obj_clear_flag(s_loading_layer, LV_OBJ_FLAG_HIDDEN);
        app_ui_set_loading_text_unlocked(text);
    }

    lv_obj_move_foreground(s_loading_layer);
    lv_refr_now(NULL);
    bsp_display_unlock();
    return true;
}

/* 加锁更新启动加载层状态文本。 */
void app_ui_set_loading_text(const char *text)
{
    if ((text == NULL) || (s_loading_layer == NULL))
    {
        return;
    }

    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    app_ui_set_loading_text_unlocked(text);
    lv_obj_move_foreground(s_loading_layer);
    lv_refr_now(NULL);
    bsp_display_unlock();
}

/* 加锁更新启动加载层进度条。 */
void app_ui_set_loading_progress(int32_t percent)
{
    if (s_loading_bar == NULL)
    {
        return;
    }

    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    lv_bar_set_value(s_loading_bar, percent, LV_ANIM_ON);
    lv_refr_now(NULL);
    bsp_display_unlock();
}

/* 加锁删除启动加载层并恢复 HUD 显示。 */
void app_ui_hide_loading(void)
{
    if (s_loading_layer == NULL)
    {
        return;
    }

    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    lv_obj_delete(s_loading_layer);
    s_loading_layer = NULL;
    s_loading_detail = NULL;
    s_loading_bar = NULL;
    lv_refr_now(NULL);
    bsp_display_unlock();
}

/* -------------------------------------------------------------------------- */
/* 主界面（仪表盘）                                                           */
/* -------------------------------------------------------------------------- */

void app_ui_set_pickup_callback(app_ui_pickup_cb_t cb)
{
    s_pickup_cb = cb;
}

static void app_ui_pickup_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (s_pickup_cb != NULL)
    {
        s_pickup_cb();
    }
}

static lv_obj_t *app_ui_create_status_dot(lv_obj_t *parent, const char *label_text, int x_offset)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x60717D), 0);
    lv_obj_set_style_text_font(lbl, &font_loading_cn, 0);
    lv_label_set_text(lbl, label_text);
    lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, x_offset, 16);

    lv_obj_t *ind = lv_obj_create(parent);
    lv_obj_set_size(ind, 11, 11);
    lv_obj_set_style_radius(ind, 6, 0);
    lv_obj_set_style_border_width(ind, 1, 0);
    lv_obj_set_style_border_color(ind, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(ind, lv_color_hex(0xD85B5B), 0);
    lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_align_to(ind, lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    return ind;
}

static lv_obj_t *app_ui_create_main_card(lv_obj_t *parent,
    const char *title,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h)
{
    lv_obj_t *card = app_ui_create_soft_panel(parent, w, h, 8);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);
    app_ui_create_panel_title(card, title);
    return card;
}

static void app_ui_update_clock_unlocked(void)
{
    if (s_main_clock_time == NULL || s_main_clock_note == NULL)
    {
        return;
    }

    time_t now = 0;
    time(&now);
    if ((int64_t)now < UI_CLOCK_VALID_EPOCH)
    {
        lv_label_set_text(s_main_clock_time, "--:--:--");
        lv_label_set_text(s_main_clock_note, "WAIT SYNC");
        lv_obj_set_style_text_color(s_main_clock_time, lv_color_hex(0x8A99A3), 0);
        return;
    }

    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_buf[16] = {0};
    char date_buf[24] = {0};
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_now);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
    lv_label_set_text(s_main_clock_time, time_buf);
    lv_label_set_text(s_main_clock_note, date_buf);
    lv_obj_set_style_text_color(s_main_clock_time, lv_color_hex(0x263544), 0);
}

static void app_ui_clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    app_ui_update_clock_unlocked();
}

static void app_ui_set_main_task_text_unlocked(const char *text, lv_color_t color)
{
    if (s_main_task_label != NULL)
    {
        lv_label_set_text(s_main_task_label, text);
        lv_obj_set_style_text_color(s_main_task_label, color, 0);
    }
    if (s_main_task_card_label != NULL)
    {
        lv_label_set_text(s_main_task_card_label, text);
        lv_obj_set_style_text_color(s_main_task_card_label, color, 0);
    }
}

bool app_ui_show_main_screen(void)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }

    lv_obj_t *scr = app_get_active_screen();

    if (s_main_layer == NULL)
    {
        s_main_layer = lv_obj_create(scr);
        lv_obj_set_size(s_main_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_set_style_bg_color(s_main_layer, lv_color_hex(0xEAF0F4), 0);
        lv_obj_set_style_bg_opa(s_main_layer, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_main_layer, 0, 0);
        lv_obj_set_style_radius(s_main_layer, 0, 0);
        lv_obj_set_style_pad_all(s_main_layer, 0, 0);
        lv_obj_clear_flag(s_main_layer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_main_layer, LV_ALIGN_CENTER, 0, 0);

        /* 顶栏 */
        lv_obj_t *topbar = app_ui_create_soft_panel(s_main_layer, BSP_LCD_H_RES, 82, 0);
        lv_obj_set_style_bg_color(topbar, lv_color_hex(0xF8FAFB), 0);
        lv_obj_set_style_border_width(topbar, 0, 0);
        lv_obj_align(topbar, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *topbar_line = lv_obj_create(s_main_layer);
        lv_obj_set_size(topbar_line, BSP_LCD_H_RES, 1);
        lv_obj_set_style_bg_color(topbar_line, lv_color_hex(0xD7E2E8), 0);
        lv_obj_set_style_bg_opa(topbar_line, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(topbar_line, 0, 0);
        lv_obj_set_style_radius(topbar_line, 0, 0);
        lv_obj_set_style_pad_all(topbar_line, 0, 0);
        lv_obj_align(topbar_line, LV_ALIGN_TOP_LEFT, 0, 82);

        /* Logo 左上角 */
        lv_obj_t *logo_panel = app_ui_create_soft_panel(s_main_layer, 124, 68, 6);
        lv_obj_set_style_bg_color(logo_panel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(logo_panel, LV_ALIGN_TOP_LEFT, 16, 7);

        lv_obj_t *img = lv_image_create(logo_panel);
        lv_image_set_src(img, &logo);
        lv_image_set_scale(img, 112);
        lv_obj_center(img);

        /* 作品名称 顶栏 */
        lv_obj_t *proj_name = lv_label_create(s_main_layer);
        lv_obj_set_width(proj_name, BSP_LCD_H_RES - 390);
#if LVGL_VERSION_MAJOR >= 9
        lv_label_set_long_mode(proj_name, LV_LABEL_LONG_MODE_WRAP);
#else
        lv_label_set_long_mode(proj_name, LV_LABEL_LONG_WRAP);
#endif
        lv_obj_set_style_text_color(proj_name, lv_color_hex(0x263544), 0);
        lv_obj_set_style_text_font(proj_name, &font_loading_cn, 0);
        lv_obj_set_style_text_align(proj_name, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(proj_name, UI_PROJECT_NAME);
        lv_obj_align(proj_name, LV_ALIGN_TOP_LEFT, 158, 20);

        /* 状态指示器 右上角 */
        s_main_ch32_ind = app_ui_create_status_dot(s_main_layer, "CH32", -22);
        s_main_mqtt_ind = app_ui_create_status_dot(s_main_layer, "云端", -100);
        s_main_wifi_ind = app_ui_create_status_dot(s_main_layer, "WiFi", -178);

        /* 主信息区 */
        lv_obj_t *hero = app_ui_create_soft_panel(s_main_layer, 392, 188, 10);
        lv_obj_set_style_bg_color(hero, lv_color_hex(0xF9FBFC), 0);
        lv_obj_align(hero, LV_ALIGN_TOP_LEFT, 28, 108);

        lv_obj_t *title = lv_label_create(hero);
        lv_obj_set_style_text_color(title, lv_color_hex(0x60717D), 0);
        lv_obj_set_style_text_font(title, &font_loading_cn, 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(title, "任务状态");
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 20);

        lv_obj_t *hero_sub = lv_label_create(hero);
        lv_obj_set_style_text_color(hero_sub, lv_color_hex(0x8A99A3), 0);
        lv_obj_set_style_text_font(hero_sub, &font_loading_cn, 0);
        lv_label_set_text(hero_sub, UI_TEAM_NAME);
        lv_obj_align(hero_sub, LV_ALIGN_TOP_LEFT, 24, 54);

        s_main_task_label = lv_label_create(hero);
        lv_obj_set_width(s_main_task_label, 210);
        lv_obj_set_style_text_color(s_main_task_label, lv_color_hex(0x263544), 0);
        lv_obj_set_style_text_font(s_main_task_label, &font_loading_cn, 0);
        lv_obj_set_style_text_align(s_main_task_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(s_main_task_label, "等待任务");
        lv_obj_align(s_main_task_label, LV_ALIGN_TOP_LEFT, 24, 100);

        /* 时钟卡片 */
        lv_obj_t *clock_card = app_ui_create_soft_panel(s_main_layer, 320, 112, 10);
        lv_obj_align(clock_card, LV_ALIGN_TOP_RIGHT, -28, 108);

        lv_obj_t *clock_title = lv_label_create(clock_card);
        lv_obj_set_style_text_color(clock_title, lv_color_hex(0x60717D), 0);
        lv_obj_set_style_text_font(clock_title, &font_loading_cn, 0);
        lv_label_set_text(clock_title, "TIME");
        lv_obj_align(clock_title, LV_ALIGN_TOP_LEFT, 18, 12);

        s_main_clock_time = lv_label_create(clock_card);
        lv_obj_set_style_text_color(s_main_clock_time, lv_color_hex(0x263544), 0);
        lv_obj_set_style_text_font(s_main_clock_time, &font_title_en, 0);
        lv_obj_set_style_text_align(s_main_clock_time, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(s_main_clock_time, "--:--:--");
        lv_obj_align(s_main_clock_time, LV_ALIGN_RIGHT_MID, -18, 6);

        s_main_clock_note = lv_label_create(clock_card);
        lv_obj_set_style_text_color(s_main_clock_note, lv_color_hex(0x8A99A3), 0);
        lv_obj_set_style_text_font(s_main_clock_note, &font_loading_cn, 0);
        lv_label_set_text(s_main_clock_note, "WAIT SYNC");
        lv_obj_align(s_main_clock_note, LV_ALIGN_BOTTOM_RIGHT, -20, -12);

        /* 天气预留卡片 */
        lv_obj_t *weather_card = app_ui_create_soft_panel(s_main_layer, 320, 64, 10);
        lv_obj_align(weather_card, LV_ALIGN_TOP_RIGHT, -28, 232);

        lv_obj_t *weather_title = lv_label_create(weather_card);
        lv_obj_set_style_text_color(weather_title, lv_color_hex(0x60717D), 0);
        lv_obj_set_style_text_font(weather_title, &font_loading_cn, 0);
        lv_label_set_text(weather_title, "WEATHER");
        lv_obj_align(weather_title, LV_ALIGN_LEFT_MID, 18, 0);

        s_main_weather_label = lv_label_create(weather_card);
        lv_obj_set_width(s_main_weather_label, 160);
        lv_obj_set_style_text_color(s_main_weather_label, lv_color_hex(0x1F6F7A), 0);
        lv_obj_set_style_text_font(s_main_weather_label, &font_loading_cn, 0);
        lv_obj_set_style_text_align(s_main_weather_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(s_main_weather_label, "--");
        lv_obj_align(s_main_weather_label, LV_ALIGN_RIGHT_MID, -18, 0);

        /* 下方状态卡片 */
        lv_obj_t *task_card = app_ui_create_main_card(s_main_layer, "任务状态", 28, 318, 224, 86);
        s_main_task_card_label = app_ui_create_panel_value(task_card, "等待任务");

        lv_obj_t *conn_card = app_ui_create_main_card(s_main_layer, "通信状态", 288, 318, 224, 86);
        s_main_conn_label = app_ui_create_panel_value(conn_card, "通信未就绪");

        lv_obj_t *dock_card = app_ui_create_main_card(s_main_layer, "接驳系统", 548, 318, 224, 86);
        s_main_dock_label = app_ui_create_panel_value(dock_card, "接驳未就绪");

        /* 取货按钮 */
        s_main_pickup_btn = app_ui_button_create(s_main_layer);
        lv_obj_set_size(s_main_pickup_btn, 136, 46);
        lv_obj_set_style_bg_color(s_main_pickup_btn, lv_color_hex(0x2D8A73), 0);
        lv_obj_set_style_bg_opa(s_main_pickup_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_main_pickup_btn, 0, 0);
        lv_obj_set_style_radius(s_main_pickup_btn, 8, 0);
        lv_obj_align(s_main_pickup_btn, LV_ALIGN_TOP_LEFT, 260, 224);
        lv_obj_add_event_cb(s_main_pickup_btn, app_ui_pickup_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_flag(s_main_pickup_btn, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *btn_label = lv_label_create(s_main_pickup_btn);
        lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(btn_label, &font_loading_cn, 0);
        lv_label_set_text(btn_label, "取货");
        lv_obj_center(btn_label);

        /* 底部信息 */
        lv_obj_t *contest = lv_label_create(s_main_layer);
        lv_obj_set_style_text_color(contest, lv_color_hex(0x8A99A3), 0);
        lv_obj_set_style_text_font(contest, &font_loading_cn, 0);
        lv_obj_set_style_text_align(contest, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(contest, UI_CONTEST_NAME);
        lv_obj_align(contest, LV_ALIGN_BOTTOM_MID, 0, -10);

        lv_obj_t *team = lv_label_create(s_main_layer);
        lv_obj_set_style_text_color(team, lv_color_hex(0x60717D), 0);
        lv_obj_set_style_text_font(team, &font_loading_cn, 0);
        lv_obj_set_style_text_align(team, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(team, UI_TEAM_NAME);
        lv_obj_align(team, LV_ALIGN_BOTTOM_MID, 0, -34);

        app_ui_update_clock_unlocked();
        if (s_main_clock_timer == NULL)
        {
            s_main_clock_timer = lv_timer_create(app_ui_clock_timer_cb, 1000, NULL);
        }
    }
    else
    {
        lv_obj_clear_flag(s_main_layer, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_move_foreground(s_main_layer);
    lv_refr_now(NULL);
    bsp_display_unlock();
    return true;
}

void app_ui_hide_main_screen(void)
{
    if (s_main_layer == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    lv_obj_add_flag(s_main_layer, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL);
    bsp_display_unlock();
}

void app_ui_main_screen_show_pickup(bool show)
{
    if (s_main_pickup_btn == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    if (show)
    {
        lv_obj_clear_flag(s_main_pickup_btn, LV_OBJ_FLAG_HIDDEN);
        app_ui_set_main_task_text_unlocked("任务完成", lv_color_hex(0x2D8A73));
    }
    else
    {
        lv_obj_add_flag(s_main_pickup_btn, LV_OBJ_FLAG_HIDDEN);
        app_ui_set_main_task_text_unlocked("等待任务", lv_color_hex(0x1F6F7A));
    }
    bsp_display_unlock();
}

void app_ui_main_screen_update_status(bool wifi_ok, bool mqtt_ok, bool ch32_ok)
{
    if (s_main_layer == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    lv_color_t green = lv_color_hex(0x2D8A73);
    lv_color_t red = lv_color_hex(0xD85B5B);
    if (s_main_wifi_ind != NULL)
    {
        lv_obj_set_style_bg_color(s_main_wifi_ind, wifi_ok ? green : red, 0);
    }
    if (s_main_mqtt_ind != NULL)
    {
        lv_obj_set_style_bg_color(s_main_mqtt_ind, mqtt_ok ? green : red, 0);
    }
    if (s_main_ch32_ind != NULL)
    {
        lv_obj_set_style_bg_color(s_main_ch32_ind, ch32_ok ? green : red, 0);
    }
    if (s_main_conn_label != NULL)
    {
        lv_label_set_text(s_main_conn_label,
            (wifi_ok && mqtt_ok) ? "通信已就绪" : "通信未就绪");
        lv_obj_set_style_text_color(s_main_conn_label,
            (wifi_ok && mqtt_ok) ? green : red,
            0);
    }
    if (s_main_dock_label != NULL)
    {
        lv_label_set_text(s_main_dock_label, ch32_ok ? "接驳已就绪" : "接驳未就绪");
        lv_obj_set_style_text_color(s_main_dock_label, ch32_ok ? green : red, 0);
    }
    bsp_display_unlock();
}

void app_ui_main_screen_set_task_text(const char *text)
{
    if (s_main_task_label == NULL || text == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    const char *display_text = text;
    lv_color_t color = lv_color_hex(0x1F6F7A);
    if (strcmp(text, "waiting task") == 0)
    {
        display_text = "等待任务";
    }
    else if (strcmp(text, "pickup failed / retry") == 0)
    {
        display_text = "取货未完成";
        color = lv_color_hex(0xD85B5B);
    }
    app_ui_set_main_task_text_unlocked(display_text, color);
    bsp_display_unlock();
}

void app_ui_main_screen_set_weather_text(const char *text)
{
    if (s_main_weather_label == NULL || text == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    lv_label_set_text(s_main_weather_label, text);
    bsp_display_unlock();
}

/* 加锁更新主状态标签。 */
void app_ui_set_status(const char *text)
{

    if ((text == NULL) || (s_status == NULL))
    {
        return;
    }

    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_label_text_unlocked(s_status, text);

    bsp_display_unlock();
}
/* 加锁更新视觉状态标签。 */
void app_ui_set_vision_text(const char *text)
{

    if ((text == NULL) || (s_vision == NULL))
    {
        return;
    }

    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_vision_text_unlocked(text);

    bsp_display_unlock();
}

/* 加锁更新抓图状态标签。 */
void app_ui_set_capture_text(const char *text)
{
    if ((text == NULL) || (s_capture == NULL))
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_capture_text_unlocked(text);
    bsp_display_unlock();
}
/* 加锁更新接驳调试标签。 */
void app_ui_set_dock_text(const char *text)
{

    if ((text == NULL) || (s_dock == NULL))
    {
        return;
    }

    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_label_text_unlocked(s_dock, text);

    bsp_display_unlock();
}
/* 根据最新视觉和接驳判定结果刷新 HUD 叠加元素。 */
static void app_ui_update_hud_unlocked(const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock)
{

    if ((dock == NULL) || (s_hud_layer == NULL) || (s_track_box == NULL))
    {
        return;
    }

    bool hold_box = false;
    bool show_box = false;
    int32_t box_x = 0;
    int32_t box_y = 0;
    int32_t box_w = 0;
    int32_t box_h = 0;

    if ((vision != NULL) && vision->valid)
    {
        app_ui_map_bbox_to_screen(vision, &box_x, &box_y, &box_w, &box_h);
        s_have_last_box = true;
        s_last_box_x = box_x;
        s_last_box_y = box_y;
        s_last_box_w = box_w;
        s_last_box_h = box_h;
        show_box = true;
    } else if (s_have_last_box && (dock->invalid_hold_count > 0U) &&
        (dock->state != APP_DOCK_STATE_SEARCHING))
        {
        box_x = s_last_box_x;
        box_y = s_last_box_y;
        box_w = s_last_box_w;
        box_h = s_last_box_h;
        show_box = true;
        hold_box = true;
    }
    else
    {
        s_have_last_box = false;
    }
    lv_color_t box_color = app_ui_state_color(dock->state, hold_box);
    lv_opa_t box_opa = hold_box ? LV_OPA_50 : LV_OPA_COVER;
    app_ui_set_track_box(show_box, box_x, box_y, box_w, box_h, box_color, box_opa);

    if ((s_cross_h != NULL) && (s_cross_v != NULL))
    {
        lv_color_t cross_color = app_ui_state_color(dock->state, false);
        lv_obj_set_style_bg_color(s_cross_h, cross_color, 0);
        lv_obj_set_style_bg_color(s_cross_v, cross_color, 0);
    }
    app_ui_update_lock_bar(dock);
    app_ui_update_auth_banner(dock->state);
}

void app_ui_update_hud(const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock)
{
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_update_hud_unlocked(vision, dock);
    bsp_display_unlock();
}

void app_ui_update_control_state(const char *status,
    const char *vision_text,
    const char *dock_text,
    const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock)
{
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_label_text_unlocked(s_status, status);
    app_ui_set_vision_text_unlocked(vision_text);
    app_ui_set_label_text_unlocked(s_dock, dock_text);
    app_ui_update_hud_unlocked(vision, dock);
    bsp_display_unlock();
}
