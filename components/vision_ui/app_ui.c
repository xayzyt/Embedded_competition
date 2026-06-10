#include "app_ui.h"
#include <stdio.h>
#include <string.h>

#include "app_ai_capture.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "lvgl.h"

// 相机预览 HUD 与启动页 UI：负责状态文字、对接框、锁定条、抓图按钮和天气保护按钮。

#ifndef BSP_CAMERA_ROTATION
#define BSP_CAMERA_ROTATION 0
#endif
LV_FONT_DECLARE(font_loading_cn)
LV_FONT_DECLARE(font_main_title_cn)
LV_FONT_DECLARE(font_title_en)
LV_IMAGE_DECLARE(logo);

#define HUD_SRC_W               320
#define HUD_SRC_H               240
#define HUD_LOCK_SEG_COUNT      2
#define HUD_AUTH_SHOW_MS        1200
#define UI_LOCK_SHORT_MS        30
#define UI_LOCK_BOOT_MS         300
#define UI_TOP_BAR_X            252
#define UI_TOP_BAR_Y            16
#define UI_TOP_BAR_W            (BSP_LCD_H_RES - UI_TOP_BAR_X - 18)
#define UI_TOP_BAR_H            42
#define UI_HUD_STATUS_W         218
#define UI_HUD_VISION_W         156
#define UI_HUD_HINT_W           172
#define UI_FLOW_X               16
#define UI_FLOW_Y               18
#define UI_FLOW_W               208
#define UI_FLOW_H               226
#define UI_FLOW_TITLE_W         184
#define UI_FLOW_TITLE_H         30
#define UI_FLOW_RULE_W          176
#define UI_FLOW_STEP_TOP        48
#define UI_FLOW_STEP_W          184
#define UI_FLOW_STEP_H          34
#define UI_FLOW_STEP_PITCH      40
#define UI_FLOW_DOT_X           14
#define UI_FLOW_DOT_Y           11
#define UI_FLOW_DOT_SIZE        11
#define UI_FLOW_LINE_W          2
#define UI_FLOW_TEXT_X          34
#define UI_FLOW_NAME_W          70
#define UI_FLOW_DETAIL_X        102
#define UI_FLOW_DETAIL_W        76
#define UI_FLOW_LABEL_Y         6
#define UI_FLOW_ACTIVE_BAR_W    3
#define UI_FLOW_ACTIVE_BAR_H    24
#define UI_RETICLE_SIZE         72
#define UI_TELEMETRY_W          660
#define UI_TELEMETRY_H          36
#define UI_TRACK_CORNER_COUNT   8
#define UI_TRACK_LABEL_W        118
#define UI_TRACK_LABEL_H        24
#define UI_CONTEST_NAME         "第九届（2026）全国大学生嵌入式芯片与系统设计竞赛"
#define UI_TEAM_NAME            "地线引力队"

static const char *TAG = "app_ui";

// HUD、流程面板和启动页对象。
static lv_obj_t *s_top_bar = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_coord = NULL;
static lv_obj_t *s_vision = NULL;
static lv_obj_t *s_dock = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_hud_layer = NULL;
static lv_obj_t *s_track_box = NULL;
static lv_obj_t *s_track_corner[UI_TRACK_CORNER_COUNT] = {0};
static lv_obj_t *s_track_label = NULL;
static lv_obj_t *s_reticle_ring = NULL;
static lv_obj_t *s_cross_h = NULL;
static lv_obj_t *s_cross_v = NULL;
static lv_obj_t *s_lock_seg[HUD_LOCK_SEG_COUNT] = {0};
static lv_obj_t *s_auth = NULL;
static lv_obj_t *s_cap_btn = NULL;
static lv_obj_t *s_stop_btn = NULL;
static lv_obj_t *s_mode_btn = NULL;
static lv_obj_t *s_weather_guard_btn = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_capture = NULL;
static lv_obj_t *s_flow_panel = NULL;
static lv_obj_t *s_flow_title = NULL;
static lv_obj_t *s_flow_title_rule = NULL;
static lv_obj_t *s_flow_step[APP_UI_FLOW_STEP_COUNT] = {0};
static lv_obj_t *s_flow_step_dot[APP_UI_FLOW_STEP_COUNT] = {0};
static lv_obj_t *s_flow_step_line[APP_UI_FLOW_STEP_COUNT - 1] = {0};
static lv_obj_t *s_flow_step_active_bar[APP_UI_FLOW_STEP_COUNT] = {0};
static lv_obj_t *s_flow_step_name[APP_UI_FLOW_STEP_COUNT] = {0};
static lv_obj_t *s_flow_step_detail[APP_UI_FLOW_STEP_COUNT] = {0};
static lv_obj_t *s_loading_layer = NULL;
static lv_obj_t *s_loading_detail = NULL;
static lv_obj_t *s_loading_bar = NULL;

static app_ui_weather_emergency_cb_t s_weather_emergency_cb = NULL;

// 上一帧识别框和认证提示状态。
static bool s_have_last_box = false;
static int32_t s_last_box_x = 0;
static int32_t s_last_box_y = 0;
static int32_t s_last_box_w = 0;
static int32_t s_last_box_h = 0;
static app_dock_state_t s_last_hud_state = APP_DOCK_STATE_SEARCHING;
static uint32_t s_auth_deadline_ms = 0;

// 控制器快照缓存，用于跳过内容未变化的 LVGL 刷新。
static bool s_control_cache_valid = false;
static char s_control_status_text[32] = {0};
static char s_control_vision_text[32] = {0};
static app_dock_state_t s_control_dock_state = APP_DOCK_STATE_SEARCHING;
static bool s_control_vision_valid = false;
static uint32_t s_control_vision_seq = 0;
static int32_t s_control_bbox_x = 0;
static int32_t s_control_bbox_y = 0;
static int32_t s_control_bbox_w = 0;
static int32_t s_control_bbox_h = 0;
static uint16_t s_control_stable_count = 0;
static uint8_t s_control_invalid_hold_count = 0;
static uint16_t s_control_tag_id = 0;
static int32_t s_control_dx = 0;
static int32_t s_control_dy = 0;
static int32_t s_control_distance_mm = 0;
static uint8_t s_control_hover_score = 0;
static uint8_t s_control_ready_pass_count = 0;
static app_ui_flow_snapshot_t s_control_flow_cache = {0};

static void app_ui_weather_emergency_event_cb(lv_event_t *e);

static lv_obj_t *app_ui_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}

/* ---------- LVGL 对象样式 ---------- */

// 以下函数只设置外观，不创建对象；调用者负责传入有效的 LVGL 对象。
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
static void app_ui_style_top_bar(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x06141B), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)172, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x5ECFE0), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)92, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x00131A), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)70, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_top_label(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
}
static void app_ui_style_cross_line(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xA7FFF0), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)116, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}
static void app_ui_style_reticle_ring(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xA7FFF0), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)86, 0);
    lv_obj_set_style_radius(obj, UI_RETICLE_SIZE / 2, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_track_box(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x63D5FF), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)70, 0);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static void app_ui_style_track_corner(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x63D5FF), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 1, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_track_label(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x06141B), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)190, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x63D5FF), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)120, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FBFF), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(obj, 8, 0);
    lv_obj_set_style_pad_ver(obj, 3, 0);
    lv_obj_set_style_radius(obj, 5, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static void app_ui_style_lock_seg(lv_obj_t *obj)
{
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x4B6B73), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)160, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x0B1A21), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)120, 0);
}
static void app_ui_style_hint_label(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)148, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x0B1A21), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xCFF8FF), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2E7C8E), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)110, 0);
    lv_obj_set_style_pad_hor(obj, 10, 0);
    lv_obj_set_style_pad_ver(obj, 5, 0);
    lv_obj_set_style_radius(obj, 6, 0);
}
static void app_ui_style_flow_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x071216), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)154, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x74DCEB), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)64, 0);
    lv_obj_set_style_radius(obj, 7, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 5, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x00131A), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)45, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_flow_title(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FBFF), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
}
static void app_ui_style_flow_rule(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)82, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 1, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_flow_step(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 5, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_flow_dot(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x71858E), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)145, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xDCECF1), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)80, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_flow_line(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x71858E), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)92, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 1, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_flow_active_bar(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x64D2FF), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)200, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
#endif
}
static void app_ui_style_flow_step_label(lv_obj_t *obj, bool primary)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, primary ? lv_color_hex(0xDCECF1) : lv_color_hex(0x95AAB2), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}
static void app_ui_style_auth_label(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x092A26), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)210, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x4FE0B0), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FFF7), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_pad_hor(obj, 14, 0);
    lv_obj_set_style_pad_ver(obj, 10, 0);
    lv_obj_set_style_radius(obj, 6, 0);
}
static void app_ui_style_telemetry_label(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x06141B), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)170, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x5ECFE0), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)86, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FBFF), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(obj, 14, 0);
    lv_obj_set_style_pad_ver(obj, 7, 0);
    lv_obj_set_style_radius(obj, 8, 0);
}
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
static void app_ui_style_loading_detail(lv_obj_t *obj)
{
    lv_obj_set_width(obj, 420);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x263544), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
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
static void app_ui_style_weather_guard_button(lv_obj_t *obj)
{
    lv_obj_set_size(obj, 116, 30);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xB91C1C), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)180, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFECACA), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)120, 0);
    lv_obj_set_style_radius(obj, 15, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
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
static void app_ui_label_set_dots(lv_obj_t *label)
{
    if (label == NULL)
    {
        return;
    }
#if LVGL_VERSION_MAJOR >= 9
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
#else
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
#endif
}

static void app_ui_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == NULL)
    {
        return;
    }
    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void app_ui_move_foreground(lv_obj_t *obj)
{
    if (obj != NULL)
    {
        lv_obj_move_foreground(obj);
    }
}

/* ---------- 文本转换与流程面板 ---------- */

// LVGL 锁内更新标签；文本未变化时跳过，减少刷新抖动。
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
static const char *app_ui_flow_step_title(app_ui_flow_step_t step)
{
    switch (step)
    {
    case APP_UI_FLOW_STEP_DRONE:
        return "无人机";
    case APP_UI_FLOW_STEP_TAG:
        return "Tag";
    case APP_UI_FLOW_STEP_EXEC:
        return "接驳";
    case APP_UI_FLOW_STEP_DONE:
        return "完成";
    default:
        return "";
    }
}

static void app_ui_flow_snapshot_default(const char *headline, app_ui_flow_snapshot_t *out)
{
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->active_step = APP_UI_FLOW_STEP_DRONE;
    for (int i = 0; i < APP_UI_FLOW_STEP_COUNT; i++)
    {
        out->step_state[i] = APP_UI_FLOW_STATE_WAITING;
    }
    snprintf(out->headline, sizeof(out->headline), "%s",
        (headline != NULL && headline[0] != '\0') ? headline : "等待云端");
    snprintf(out->step_detail[APP_UI_FLOW_STEP_DRONE], sizeof(out->step_detail[0]), "等待");
    snprintf(out->step_detail[APP_UI_FLOW_STEP_TAG], sizeof(out->step_detail[0]), "等待Tag");
    snprintf(out->step_detail[APP_UI_FLOW_STEP_EXEC], sizeof(out->step_detail[0]), "等待接驳");
    snprintf(out->step_detail[APP_UI_FLOW_STEP_DONE], sizeof(out->step_detail[0]), "未完成");
}

static bool app_ui_flow_snapshot_equal(const app_ui_flow_snapshot_t *a,
    const app_ui_flow_snapshot_t *b)
{
    if (a == b)
    {
        return true;
    }
    if (a == NULL || b == NULL)
    {
        return false;
    }
    return memcmp(a, b, sizeof(*a)) == 0;
}

static lv_color_t app_ui_flow_state_color(app_ui_flow_state_t state)
{
    switch (state)
    {
    case APP_UI_FLOW_STATE_ACTIVE:
        return lv_color_hex(0x64D2FF);
    case APP_UI_FLOW_STATE_DONE:
        return lv_color_hex(0x4FE0B0);
    case APP_UI_FLOW_STATE_ERROR:
        return lv_color_hex(0xF87171);
    case APP_UI_FLOW_STATE_WAITING:
    default:
        return lv_color_hex(0x71858E);
    }
}

static const char *app_ui_flow_headline_display_text(const char *headline)
{
    if (headline == NULL || headline[0] == '\0')
    {
        return "等待";
    }
    if (strcmp(headline, "等待云端") == 0 || strcmp(headline, "系统启动") == 0)
    {
        return "等待";
    }
    if (strcmp(headline, "接驳完成") == 0)
    {
        return "完成";
    }
    if (strcmp(headline, "接驳ERR") == 0)
    {
        return "ERR";
    }
    if (strcmp(headline, "接驳OK") == 0)
    {
        return "OK";
    }
    return headline;
}

static const char *app_ui_flow_detail_display_text(app_ui_flow_step_t step, const char *detail)
{
    if (detail == NULL || detail[0] == '\0')
    {
        return "等待";
    }
    if (strcmp(detail, "等待Tag") == 0)
    {
        return "等待";
    }
    if (step == APP_UI_FLOW_STEP_EXEC &&
        (strcmp(detail, "等待接驳") == 0 || strcmp(detail, "接驳等待") == 0))
    {
        return "等待";
    }
    if (strcmp(detail, "接驳完成") == 0)
    {
        return "完成";
    }
    if (strcmp(detail, "接驳ERR") == 0)
    {
        return "ERR";
    }
    if (strcmp(detail, "接驳OK") == 0)
    {
        return "OK";
    }
    return detail;
}

static void app_ui_update_flow_step_unlocked(uint32_t index,
    app_ui_flow_state_t state,
    bool active,
    const char *detail)
{
    if (index >= APP_UI_FLOW_STEP_COUNT || s_flow_step[index] == NULL)
    {
        return;
    }
    const lv_color_t color = app_ui_flow_state_color(state);
    lv_obj_set_style_bg_color(s_flow_step[index],
        active ? lv_color_hex(0x0B2730) : lv_color_hex(0x071216),
        0);
    lv_obj_set_style_bg_opa(s_flow_step[index],
        active ? (lv_opa_t)92 : LV_OPA_TRANSP,
        0);
    lv_obj_set_style_border_width(s_flow_step[index], 0, 0);
    lv_obj_set_style_border_opa(s_flow_step[index], LV_OPA_TRANSP, 0);
    if (s_flow_step_dot[index] != NULL)
    {
        lv_obj_set_style_bg_color(s_flow_step_dot[index], color, 0);
        lv_obj_set_style_bg_opa(s_flow_step_dot[index],
            active ? (lv_opa_t)235 :
            (state == APP_UI_FLOW_STATE_WAITING ? (lv_opa_t)92 : (lv_opa_t)205),
            0);
        lv_obj_set_style_border_color(s_flow_step_dot[index],
            active ? lv_color_hex(0xE9FBFF) : color,
            0);
        lv_obj_set_style_border_opa(s_flow_step_dot[index],
            active ? (lv_opa_t)210 : (lv_opa_t)95,
            0);
        lv_obj_set_style_border_width(s_flow_step_dot[index], active ? 2 : 1, 0);
    }
    if (s_flow_step_active_bar[index] != NULL)
    {
        if (active)
        {
            lv_obj_clear_flag(s_flow_step_active_bar[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_flow_step_active_bar[index], color, 0);
            lv_obj_set_style_bg_opa(s_flow_step_active_bar[index], (lv_opa_t)220, 0);
        }
        else
        {
            lv_obj_add_flag(s_flow_step_active_bar[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (index < (APP_UI_FLOW_STEP_COUNT - 1) && s_flow_step_line[index] != NULL)
    {
        const bool completed_line = (state == APP_UI_FLOW_STATE_DONE);
        lv_obj_set_style_bg_color(s_flow_step_line[index],
            completed_line ? lv_color_hex(0x4FE0B0) : lv_color_hex(0x71858E),
            0);
        lv_obj_set_style_bg_opa(s_flow_step_line[index],
            completed_line ? (lv_opa_t)190 : (lv_opa_t)76,
            0);
    }
    if (s_flow_step_name[index] != NULL)
    {
        lv_obj_set_style_text_color(s_flow_step_name[index],
            (state == APP_UI_FLOW_STATE_ERROR) ? color :
            active ? lv_color_hex(0xE9FBFF) :
            (state == APP_UI_FLOW_STATE_WAITING ? lv_color_hex(0xB8C8CE) : color),
            0);
        lv_obj_set_style_text_font(s_flow_step_name[index], &font_loading_cn, 0);
    }
    if (s_flow_step_detail[index] != NULL)
    {
        lv_obj_set_style_text_color(s_flow_step_detail[index],
            (state == APP_UI_FLOW_STATE_ERROR) ? color :
            active ? lv_color_hex(0xDFF7FF) :
            (state == APP_UI_FLOW_STATE_DONE ? lv_color_hex(0xBFF7E3) : lv_color_hex(0x9DB0B7)),
            0);
        app_ui_set_label_text_unlocked(s_flow_step_detail[index],
            app_ui_flow_detail_display_text((app_ui_flow_step_t)index, detail));
    }
}

static void app_ui_update_flow_unlocked(const app_ui_flow_snapshot_t *flow)
{
    app_ui_flow_snapshot_t fallback = {0};
    if (flow == NULL)
    {
        app_ui_flow_snapshot_default("等待云端", &fallback);
        flow = &fallback;
    }
    if (s_flow_title != NULL)
    {
        app_ui_set_label_text_unlocked(s_flow_title,
            app_ui_flow_headline_display_text(flow->headline));
    }
    for (uint32_t i = 0; i < APP_UI_FLOW_STEP_COUNT; i++)
    {
        app_ui_update_flow_step_unlocked(i,
            flow->step_state[i],
            ((app_ui_flow_step_t)i == flow->active_step),
            flow->step_detail[i]);
    }
}

static bool app_ui_text_has(const char *text, const char *needle)
{
    return (text != NULL) && (needle != NULL) && (strstr(text, needle) != NULL);
}

// 控制器保持稳定的英文状态字符串，显示层在这里集中转换为现场文案。
static const char *app_ui_task_text_from_dock(const app_dock_judge_result_t *dock)
{
    if (dock == NULL)
    {
        return "云端接驳";
    }
    switch (dock->state) {
    case APP_DOCK_STATE_WRONG_ID:
        return "Tag 未鉴权";
    case APP_DOCK_STATE_TRACKING:
        return "对准中";
    case APP_DOCK_STATE_ALIGNED:
    case APP_DOCK_STATE_READY_TO_DOCK:
        return "接驳准备";
    case APP_DOCK_STATE_SEARCHING:
    default:
        return "等待无人机";
    }
}
static const char *app_ui_vision_text_from_dock(const app_dock_judge_result_t *dock)
{
    if (dock == NULL)
    {
        return "视觉就绪";
    }
    switch (dock->state) {
    case APP_DOCK_STATE_WRONG_ID:
        return "Tag 未鉴权";
    case APP_DOCK_STATE_TRACKING:
        return "对准中";
    case APP_DOCK_STATE_ALIGNED:
    case APP_DOCK_STATE_READY_TO_DOCK:
        return "Tag 已对准";
    case APP_DOCK_STATE_SEARCHING:
    default:
        return dock->vision_valid ? "视觉就绪" : "视觉等待";
    }
}
static const char *app_ui_status_display_text(const char *text,
    const app_dock_judge_result_t *dock)
{
    if ((text == NULL) || (text[0] == '\0'))
    {
        return "系统就绪";
    }
    if (app_ui_text_has(text, "booting") ||
        app_ui_text_has(text, "init") ||
        app_ui_text_has(text, "启动"))
    {
        return "系统启动";
    }
    if (app_ui_text_has(text, "camera start failed") ||
        app_ui_text_has(text, "camera failed"))
    {
        return "摄像头未启动";
    }
    if (app_ui_text_has(text, "cloud offline") ||
        app_ui_text_has(text, "cloud start failed") ||
        app_ui_text_has(text, "local mode"))
    {
        return "云端未连接";
    }
    if (app_ui_text_has(text, "completed"))
    {
        return "接驳完成";
    }
    if (app_ui_text_has(text, "auth passed"))
    {
        return "接驳就绪";
    }
    if (app_ui_text_has(text, "docking"))
    {
        return "接驳中";
    }
    if (app_ui_text_has(text, "fault") ||
        app_ui_text_has(text, "err") ||
        app_ui_text_has(text, "failed"))
    {
        return "系统未就绪";
    }
    if (app_ui_text_has(text, "configured") ||
        app_ui_text_has(text, "idle"))
    {
        return "系统就绪";
    }
    if (app_ui_text_has(text, "cooldown"))
    {
        return "接驳等待";
    }
    if (app_ui_text_has(text, "wait id") ||
        app_ui_text_has(text, "wait_approach") ||
        app_ui_text_has(text, "searching target") ||
        app_ui_text_has(text, "align target") ||
        app_ui_text_has(text, "hold hover") ||
        app_ui_text_has(text, "move target") ||
        app_ui_text_has(text, "dock:"))
    {
        return app_ui_task_text_from_dock(dock);
    }
    return "云端接驳";
}
static const char *app_ui_vision_display_text(const char *text,
    const app_dock_judge_result_t *dock)
{
    if (dock != NULL)
    {
        return app_ui_vision_text_from_dock(dock);
    }
    if ((text == NULL) || (text[0] == '\0'))
    {
        return "视觉就绪";
    }
    if (app_ui_text_has(text, "id:"))
    {
        return "Tag 已对准";
    }
    if (app_ui_text_has(text, "lost") ||
        app_ui_text_has(text, "wait") ||
        app_ui_text_has(text, "frame") ||
        app_ui_text_has(text, "waiting"))
    {
        return "视觉等待";
    }
    if (app_ui_text_has(text, "configured") ||
        app_ui_text_has(text, "init"))
    {
        return "视觉等待";
    }
    return "视觉就绪";
}
static int32_t app_ui_distance_cm_from_dock(const app_dock_judge_result_t *dock)
{
    if (dock == NULL || dock->est_distance_mm <= 0)
    {
        return -1;
    }
    return (dock->est_distance_mm + 5) / 10;
}
static void app_ui_format_header_dock(const char *dock_text,
    const app_dock_judge_result_t *dock,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0)
    {
        return;
    }
    if (dock != NULL)
    {
        int32_t distance_cm = app_ui_distance_cm_from_dock(dock);
        switch (dock->state) {
        case APP_DOCK_STATE_WRONG_ID:
            snprintf(out, out_size, "Tag 未鉴权");
            return;
        case APP_DOCK_STATE_TRACKING:
            if (distance_cm > 0)
            {
                snprintf(out, out_size, "Tag %ldcm", (long)distance_cm);
            }
            else
            {
                snprintf(out, out_size, "Tag 对准中");
            }
            return;
        case APP_DOCK_STATE_ALIGNED:
            snprintf(out, out_size, "Tag 已对准");
            return;
        case APP_DOCK_STATE_READY_TO_DOCK:
            snprintf(out, out_size, "READY");
            return;
        case APP_DOCK_STATE_SEARCHING:
        default:
            snprintf(out, out_size, "Tag 等待");
            return;
        }
    }
    if (dock_text != NULL && dock_text[0] != '\0')
    {
        if (app_ui_text_has(dock_text, "err") || app_ui_text_has(dock_text, "ERR"))
        {
            snprintf(out, out_size, "接驳异常");
        }
        else if (app_ui_text_has(dock_text, "ready") || app_ui_text_has(dock_text, "READY"))
        {
            snprintf(out, out_size, "接驳就绪");
        }
        else
        {
            snprintf(out, out_size, "接驳等待");
        }
        return;
    }
    snprintf(out, out_size, "接驳等待");
}
static void app_ui_set_header_unlocked(const char *status_text,
    const char *vision_text,
    const char *dock_text,
    const app_dock_judge_result_t *dock)
{
    char dock_buf[48] = {0};
    app_ui_set_label_text_unlocked(s_status,
        (status_text != NULL && status_text[0] != '\0') ? status_text : "系统就绪");
    app_ui_set_label_text_unlocked(s_vision,
        (vision_text != NULL && vision_text[0] != '\0') ? vision_text : "视觉等待");
    app_ui_format_header_dock(dock_text, dock, dock_buf, sizeof(dock_buf));
    app_ui_set_label_text_unlocked(s_hint, dock_buf);
}
static void app_ui_format_track_label(const app_dock_judge_result_t *dock,
    bool hold_box,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0)
    {
        return;
    }
    if (hold_box)
    {
        snprintf(out, out_size, "HOLD");
        return;
    }
    if (dock == NULL)
    {
        snprintf(out, out_size, "Tag");
        return;
    }
    int32_t distance_cm = app_ui_distance_cm_from_dock(dock);
    switch (dock->state) {
    case APP_DOCK_STATE_READY_TO_DOCK:
        snprintf(out, out_size, "READY");
        break;
    case APP_DOCK_STATE_WRONG_ID:
        snprintf(out, out_size, "Tag ERR");
        break;
    case APP_DOCK_STATE_TRACKING:
    case APP_DOCK_STATE_ALIGNED:
        if (distance_cm > 0)
        {
            snprintf(out, out_size, "Tag %ldcm", (long)distance_cm);
        }
        else
        {
            snprintf(out, out_size, "Tag %u", (unsigned)dock->tag_id);
        }
        break;
    case APP_DOCK_STATE_SEARCHING:
    default:
        snprintf(out, out_size, "Tag %u", (unsigned)dock->tag_id);
        break;
    }
}
static void app_ui_format_telemetry(const app_dock_judge_result_t *dock,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0)
    {
        return;
    }
    if (dock == NULL)
    {
        snprintf(out, out_size, "等待无人机 / Tag | dx -- dy -- | dist -- | lock --");
        return;
    }
    int32_t distance_cm = app_ui_distance_cm_from_dock(dock);
    const char *phase = "等待无人机 / Tag";
    switch (dock->state) {
    case APP_DOCK_STATE_WRONG_ID:
        phase = "Tag 未鉴权";
        break;
    case APP_DOCK_STATE_TRACKING:
        phase = "Tag 对准中";
        break;
    case APP_DOCK_STATE_ALIGNED:
        phase = "Tag 已对准";
        break;
    case APP_DOCK_STATE_READY_TO_DOCK:
        phase = "READY";
        break;
    case APP_DOCK_STATE_SEARCHING:
    default:
        phase = dock->vision_valid ? "Tag 等待" : "等待无人机 / Tag";
        break;
    }
    if (distance_cm > 0)
    {
        snprintf(out,
            out_size,
            "%s | dx %+ld dy %+ld | dist %ldcm | lock %u/%u",
            phase,
            (long)dock->dx,
            (long)dock->dy,
            (long)distance_cm,
            (unsigned)dock->stable_count,
            (unsigned)HUD_LOCK_SEG_COUNT);
    }
    else
    {
        snprintf(out,
            out_size,
            "%s | dx %+ld dy %+ld | dist -- | lock %u/%u",
            phase,
            (long)dock->dx,
            (long)dock->dy,
            (unsigned)dock->stable_count,
            (unsigned)HUD_LOCK_SEG_COUNT);
    }
}
static void app_ui_update_telemetry_unlocked(const app_dock_judge_result_t *dock)
{
    char text[112] = {0};
    app_ui_format_telemetry(dock, text, sizeof(text));
    app_ui_set_label_text_unlocked(s_dock, text);
}
static void app_ui_set_vision_text_unlocked(const char *text)
{
    app_ui_set_label_text_unlocked(s_vision, app_ui_vision_display_text(text, NULL));
}
static bool app_ui_control_needs_update(const char *status_text,
    const char *vision_text,
    const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock,
    const app_ui_flow_snapshot_t *flow)
{
    if (!s_control_cache_valid || (s_auth_deadline_ms != 0U))
    {
        return true;
    }
    if (!app_ui_flow_snapshot_equal(flow, &s_control_flow_cache))
    {
        return true;
    }
    if ((status_text != NULL) && (strcmp(s_control_status_text, status_text) != 0))
    {
        return true;
    }
    if ((vision_text != NULL) && (strcmp(s_control_vision_text, vision_text) != 0))
    {
        return true;
    }
    if ((dock != NULL) &&
        ((dock->state != s_control_dock_state) ||
         (dock->stable_count != s_control_stable_count) ||
         (dock->invalid_hold_count != s_control_invalid_hold_count) ||
         (dock->tag_id != s_control_tag_id) ||
         (dock->dx != s_control_dx) ||
         (dock->dy != s_control_dy) ||
         (dock->est_distance_mm != s_control_distance_mm) ||
         (dock->hover_score != s_control_hover_score) ||
         (dock->ready_pass_count != s_control_ready_pass_count)))
    {
        return true;
    }
    const bool vision_valid = (vision != NULL) && vision->valid;
    if (vision_valid != s_control_vision_valid)
    {
        return true;
    }
    if (vision_valid &&
        ((vision->frame_seq != s_control_vision_seq) ||
         (vision->bbox_x != s_control_bbox_x) ||
         (vision->bbox_y != s_control_bbox_y) ||
         (vision->bbox_w != s_control_bbox_w) ||
         (vision->bbox_h != s_control_bbox_h)))
    {
        return true;
    }
    return false;
}
static void app_ui_store_control_cache(const char *status_text,
    const char *vision_text,
    const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock,
    const app_ui_flow_snapshot_t *flow)
{
    snprintf(s_control_status_text,
        sizeof(s_control_status_text),
        "%s",
        status_text != NULL ? status_text : "");
    snprintf(s_control_vision_text,
        sizeof(s_control_vision_text),
        "%s",
        vision_text != NULL ? vision_text : "");
    if (dock != NULL)
    {
        s_control_dock_state = dock->state;
        s_control_stable_count = dock->stable_count;
        s_control_invalid_hold_count = dock->invalid_hold_count;
        s_control_tag_id = dock->tag_id;
        s_control_dx = dock->dx;
        s_control_dy = dock->dy;
        s_control_distance_mm = dock->est_distance_mm;
        s_control_hover_score = dock->hover_score;
        s_control_ready_pass_count = dock->ready_pass_count;
    }
    s_control_vision_valid = (vision != NULL) && vision->valid;
    if (s_control_vision_valid)
    {
        s_control_vision_seq = vision->frame_seq;
        s_control_bbox_x = vision->bbox_x;
        s_control_bbox_y = vision->bbox_y;
        s_control_bbox_w = vision->bbox_w;
        s_control_bbox_h = vision->bbox_h;
    }
    else
    {
        s_control_vision_seq = 0;
        s_control_bbox_x = 0;
        s_control_bbox_y = 0;
        s_control_bbox_w = 0;
        s_control_bbox_h = 0;
    }
    if (flow != NULL)
    {
        s_control_flow_cache = *flow;
    }
    else
    {
        app_ui_flow_snapshot_default(status_text, &s_control_flow_cache);
    }
    s_control_cache_valid = true;
}

/* ---------- 抓图按钮事件 ---------- */

// 抓图按钮：启动连续样本采集。
static void app_ui_capture_start_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }
    app_ui_set_vision_text_unlocked("cap: starting");
    esp_err_t ret = app_ai_capture_start();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "CAP start failed: %s", esp_err_to_name(ret));
        app_ui_set_vision_text_unlocked("cap: start fail");
    }
}
static void app_ui_capture_stop_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }
    app_ai_capture_stop();
}
static void app_ui_capture_mode_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }
    app_ai_capture_mode_t mode = app_ai_capture_toggle_mode();
    const char *label = app_ai_capture_mode_label(mode);
    if (s_mode_label != NULL)
    {
        lv_label_set_text(s_mode_label, label);
        lv_obj_center(s_mode_label);
    }
}
// 根据对接状态选择 HUD 颜色；hold_box 用灰色表示短时丢帧保持。
static lv_color_t app_ui_state_color(app_dock_state_t state, bool hold_box)
{
    if (hold_box)
    {
        return lv_color_hex(0x7E8A93);
    }
    switch (state) {
    case APP_DOCK_STATE_WRONG_ID:
        return lv_color_hex(0xF87171);
    case APP_DOCK_STATE_TRACKING:
        return lv_color_hex(0xF4C95D);
    case APP_DOCK_STATE_ALIGNED:
        return lv_color_hex(0x64D2FF);
    case APP_DOCK_STATE_READY_TO_DOCK:
        return lv_color_hex(0x4FE0B0);
    case APP_DOCK_STATE_SEARCHING:
    default:
        return lv_color_hex(0xA6B2BD);
    }
}
/* ---------- 相机坐标到屏幕坐标映射 ---------- */

// 计算相机画面在 LCD 上等比例显示后的尺寸。
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
// 按 BSP 摄像头旋转角把原图点转换到预览方向。
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
static int32_t app_ui_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

// 将灰度检测坐标系下的 bbox 映射回 LCD 坐标系，包含裁剪和旋转修正。
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
    for (int i = 0; i < 4; i++)
    {
        float rx = 0.0f;
        float ry = 0.0f;
        app_ui_transform_src_point(pts[i][0], pts[i][1], src_w, src_h, &rx, &ry);
        float sx = (float)off_x + rx * (float)fit_w / (float)rot_w;
        float sy = (float)off_y + ry * (float)fit_h / (float)rot_h;
        if (sx < min_x)
        {
            min_x = sx;
        }
        if (sy < min_y)
        {
            min_y = sy;
        }
        if (sx > max_x)
        {
            max_x = sx;
        }
        if (sy > max_y)
        {
            max_y = sy;
        }
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
// 锁定条表示稳定帧数/ready 状态，给用户一个靠近完成度反馈。
static void app_ui_update_lock_bar(const app_dock_judge_result_t *dock)
{
    uint8_t filled = 0;
    lv_color_t active = lv_color_hex(0xF4C95D);
    if (dock != NULL)
    {
        filled = (dock->stable_count > HUD_LOCK_SEG_COUNT) ? HUD_LOCK_SEG_COUNT : (uint8_t)dock->stable_count;
        if (dock->state == APP_DOCK_STATE_READY_TO_DOCK)
        {
            active = lv_color_hex(0x4FE0B0);
            filled = HUD_LOCK_SEG_COUNT;
        }
        else if (dock->state == APP_DOCK_STATE_ALIGNED)
        {
            active = lv_color_hex(0x64D2FF);
        }
        else if (dock->state == APP_DOCK_STATE_WRONG_ID)
        {
            active = lv_color_hex(0xF87171);
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
            lv_obj_set_style_bg_opa(s_lock_seg[i], (lv_opa_t)210, 0);
            lv_obj_set_style_border_color(s_lock_seg[i], active, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(s_lock_seg[i], lv_color_hex(0x0B1A21), 0);
            lv_obj_set_style_bg_opa(s_lock_seg[i], (lv_opa_t)120, 0);
            lv_obj_set_style_border_color(s_lock_seg[i], lv_color_hex(0x4B6B73), 0);
        }
    }
}
// 进入 ready 的瞬间短暂显示中文接驳提示。
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
        s_auth_deadline_ms = 0;
    }
    s_last_hud_state = state;
}
static void app_ui_set_track_box(bool show,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    lv_color_t color,
    lv_opa_t opa,
    const char *label_text)
{
    if (s_track_box == NULL)
    {
        return;
    }
    if (!show)
    {
        lv_obj_add_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < UI_TRACK_CORNER_COUNT; i++) {
            if (s_track_corner[i] != NULL)
            {
                lv_obj_add_flag(s_track_corner[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_track_label != NULL)
        {
            lv_obj_add_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_set_pos(s_track_box, x, y);
    lv_obj_set_size(s_track_box, w, h);
    lv_obj_set_style_border_color(s_track_box, color, 0);
    lv_obj_set_style_border_opa(s_track_box, (lv_opa_t)(opa / 3U), 0);
    lv_obj_clear_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
    const int32_t corner_len = app_ui_clamp_i32(((w < h) ? w : h) / 3, 14, 32);
    const int32_t corner_thick = 3;
    const int32_t corner_pos[UI_TRACK_CORNER_COUNT][4] = {
        {x, y, corner_len, corner_thick},
        {x, y, corner_thick, corner_len},
        {x + w - corner_len, y, corner_len, corner_thick},
        {x + w - corner_thick, y, corner_thick, corner_len},
        {x, y + h - corner_thick, corner_len, corner_thick},
        {x, y + h - corner_len, corner_thick, corner_len},
        {x + w - corner_len, y + h - corner_thick, corner_len, corner_thick},
        {x + w - corner_thick, y + h - corner_len, corner_thick, corner_len},
    };
    for (int i = 0; i < UI_TRACK_CORNER_COUNT; i++) {
        if (s_track_corner[i] == NULL)
        {
            continue;
        }
        lv_obj_set_pos(s_track_corner[i], corner_pos[i][0], corner_pos[i][1]);
        lv_obj_set_size(s_track_corner[i], corner_pos[i][2], corner_pos[i][3]);
        lv_obj_set_style_bg_color(s_track_corner[i], color, 0);
        lv_obj_set_style_bg_opa(s_track_corner[i], opa, 0);
        lv_obj_clear_flag(s_track_corner[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_track_label != NULL && label_text != NULL && label_text[0] != '\0')
    {
        const int32_t top_limit = UI_TOP_BAR_Y + UI_TOP_BAR_H + 6;
        int32_t label_x = app_ui_clamp_i32(x, 4, BSP_LCD_H_RES - UI_TRACK_LABEL_W - 4);
        int32_t label_y = (y > (top_limit + UI_TRACK_LABEL_H + 4)) ?
            (y - UI_TRACK_LABEL_H - 4) :
            (y + 4);
        label_y = app_ui_clamp_i32(label_y, top_limit, BSP_LCD_V_RES - UI_TRACK_LABEL_H - 4);
        lv_obj_set_width(s_track_label, UI_TRACK_LABEL_W);
        app_ui_set_label_text_unlocked(s_track_label, label_text);
        lv_obj_set_pos(s_track_label, label_x, label_y);
        lv_obj_set_style_border_color(s_track_label, color, 0);
        lv_obj_set_style_text_color(s_track_label, color, 0);
        lv_obj_clear_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
    }
}
/* ---------- 公共 UI 生命周期 ---------- */

// 创建预览 HUD 和右侧调试按钮；对象只创建一次，后续复用。
bool app_ui_create(void)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }
    lv_obj_t *scr = app_ui_get_active_screen();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
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
    for (int i = 0; i < UI_TRACK_CORNER_COUNT; i++) {
        if (s_track_corner[i] == NULL)
        {
            s_track_corner[i] = lv_obj_create(s_hud_layer);
            app_ui_style_track_corner(s_track_corner[i]);
        }
    }
    if (s_track_label == NULL)
    {
        s_track_label = lv_label_create(s_hud_layer);
        app_ui_style_track_label(s_track_label);
        app_ui_label_set_dots(s_track_label);
        lv_label_set_text(s_track_label, "Tag");
    }
    if (s_reticle_ring == NULL)
    {
        s_reticle_ring = lv_obj_create(s_hud_layer);
        app_ui_style_reticle_ring(s_reticle_ring);
        lv_obj_set_size(s_reticle_ring, UI_RETICLE_SIZE, UI_RETICLE_SIZE);
        lv_obj_align(s_reticle_ring, LV_ALIGN_CENTER, 0, 0);
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
    const int32_t seg_w = 28;
    const int32_t seg_h = 8;
    const int32_t seg_gap = 6;
    const int32_t total_w = (HUD_LOCK_SEG_COUNT * seg_w) + ((HUD_LOCK_SEG_COUNT - 1) * seg_gap);
    const int32_t start_x = BSP_LCD_H_RES - total_w - 40;
    const int32_t y = UI_TOP_BAR_Y + 17;
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {
        if (s_lock_seg[i] == NULL)
        {
            s_lock_seg[i] = lv_obj_create(scr);
            app_ui_style_lock_seg(s_lock_seg[i]);
            lv_obj_set_size(s_lock_seg[i], seg_w, seg_h);
            lv_obj_set_pos(s_lock_seg[i], start_x + i * (seg_w + seg_gap), y);
        }
    }
    if (s_flow_panel == NULL)
    {
        s_flow_panel = lv_obj_create(scr);
        app_ui_style_flow_panel(s_flow_panel);
        lv_obj_set_size(s_flow_panel, UI_FLOW_W, UI_FLOW_H);
        lv_obj_set_pos(s_flow_panel, UI_FLOW_X, UI_FLOW_Y);
    }
    if (s_flow_title == NULL)
    {
        s_flow_title = lv_label_create(s_flow_panel);
        app_ui_style_flow_title(s_flow_title);
        lv_obj_set_size(s_flow_title, UI_FLOW_TITLE_W, UI_FLOW_TITLE_H);
        app_ui_label_set_dots(s_flow_title);
        lv_label_set_text(s_flow_title, "等待");
        lv_obj_set_pos(s_flow_title, 12, 8);
    }
    if (s_flow_title_rule == NULL)
    {
        s_flow_title_rule = lv_obj_create(s_flow_panel);
        app_ui_style_flow_rule(s_flow_title_rule);
        lv_obj_set_size(s_flow_title_rule, UI_FLOW_RULE_W, 1);
        lv_obj_set_pos(s_flow_title_rule, 16, 42);
    }
    for (uint32_t i = 0; i < APP_UI_FLOW_STEP_COUNT; i++) {
        const int32_t step_y = UI_FLOW_STEP_TOP + (int32_t)i * UI_FLOW_STEP_PITCH;
        if (i < (APP_UI_FLOW_STEP_COUNT - 1) && s_flow_step_line[i] == NULL)
        {
            s_flow_step_line[i] = lv_obj_create(s_flow_panel);
            app_ui_style_flow_line(s_flow_step_line[i]);
            lv_obj_set_size(s_flow_step_line[i],
                UI_FLOW_LINE_W,
                UI_FLOW_STEP_PITCH - UI_FLOW_DOT_SIZE + 1);
            lv_obj_set_pos(s_flow_step_line[i],
                12 + UI_FLOW_DOT_X + (UI_FLOW_DOT_SIZE / 2) - (UI_FLOW_LINE_W / 2),
                step_y + UI_FLOW_DOT_Y + UI_FLOW_DOT_SIZE);
        }
        if (s_flow_step[i] == NULL)
        {
            s_flow_step[i] = lv_obj_create(s_flow_panel);
            app_ui_style_flow_step(s_flow_step[i]);
            lv_obj_set_size(s_flow_step[i], UI_FLOW_STEP_W, UI_FLOW_STEP_H);
            lv_obj_set_pos(s_flow_step[i], 12, step_y);
        }
        if (s_flow_step_active_bar[i] == NULL)
        {
            s_flow_step_active_bar[i] = lv_obj_create(s_flow_step[i]);
            app_ui_style_flow_active_bar(s_flow_step_active_bar[i]);
            lv_obj_set_size(s_flow_step_active_bar[i], UI_FLOW_ACTIVE_BAR_W, UI_FLOW_ACTIVE_BAR_H);
            lv_obj_set_pos(s_flow_step_active_bar[i], 0, 5);
        }
        if (s_flow_step_dot[i] == NULL)
        {
            s_flow_step_dot[i] = lv_obj_create(s_flow_step[i]);
            app_ui_style_flow_dot(s_flow_step_dot[i]);
            lv_obj_set_size(s_flow_step_dot[i], UI_FLOW_DOT_SIZE, UI_FLOW_DOT_SIZE);
            lv_obj_set_pos(s_flow_step_dot[i], UI_FLOW_DOT_X, UI_FLOW_DOT_Y);
        }
        if (s_flow_step_name[i] == NULL)
        {
            s_flow_step_name[i] = lv_label_create(s_flow_step[i]);
            app_ui_style_flow_step_label(s_flow_step_name[i], true);
            lv_obj_set_width(s_flow_step_name[i], UI_FLOW_NAME_W);
            app_ui_label_set_dots(s_flow_step_name[i]);
            lv_label_set_text(s_flow_step_name[i], app_ui_flow_step_title((app_ui_flow_step_t)i));
            lv_obj_set_pos(s_flow_step_name[i], UI_FLOW_TEXT_X, UI_FLOW_LABEL_Y);
        }
        if (s_flow_step_detail[i] == NULL)
        {
            s_flow_step_detail[i] = lv_label_create(s_flow_step[i]);
            app_ui_style_flow_step_label(s_flow_step_detail[i], false);
            lv_obj_set_width(s_flow_step_detail[i], UI_FLOW_DETAIL_W);
            app_ui_label_set_dots(s_flow_step_detail[i]);
            lv_label_set_text(s_flow_step_detail[i], "等待");
            lv_obj_set_style_text_align(s_flow_step_detail[i], LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_pos(s_flow_step_detail[i], UI_FLOW_DETAIL_X, UI_FLOW_LABEL_Y);
        }
        lv_obj_move_foreground(s_flow_step[i]);
    }
    app_ui_flow_snapshot_t initial_flow = {0};
    app_ui_flow_snapshot_default("系统启动", &initial_flow);
    app_ui_update_flow_unlocked(&initial_flow);
    if (s_top_bar == NULL)
    {
        s_top_bar = lv_obj_create(scr);
        app_ui_style_top_bar(s_top_bar);
        lv_obj_set_size(s_top_bar, UI_TOP_BAR_W, UI_TOP_BAR_H);
        lv_obj_set_pos(s_top_bar, UI_TOP_BAR_X, UI_TOP_BAR_Y);
    }
    if (s_auth == NULL)
    {
        s_auth = lv_label_create(scr);
        app_ui_style_auth_label(s_auth);
        lv_label_set_text(s_auth, "接驳就绪");
        lv_obj_align(s_auth, LV_ALIGN_CENTER, 0, -32);
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_status == NULL)
    {
        s_status = lv_label_create(s_top_bar != NULL ? s_top_bar : scr);
        app_ui_style_top_label(s_status, lv_color_hex(0xE9FBFF));
        lv_obj_set_width(s_status, UI_HUD_STATUS_W);
        app_ui_label_set_dots(s_status);
        lv_label_set_text(s_status, "系统启动");
        lv_obj_set_pos(s_status, 18, 11);
    }
    if (s_vision == NULL)
    {
        s_vision = lv_label_create(s_top_bar != NULL ? s_top_bar : scr);
        app_ui_style_top_label(s_vision, lv_color_hex(0xBFF7E3));
        lv_obj_set_width(s_vision, UI_HUD_VISION_W);
        app_ui_label_set_dots(s_vision);
        lv_label_set_text(s_vision, "视觉等待");
        lv_obj_set_pos(s_vision, 270, 11);
    }
    if (s_dock == NULL)
    {
        s_dock = lv_label_create(scr);
        app_ui_style_telemetry_label(s_dock);
        lv_label_set_text(s_dock, "等待无人机 / Tag | dx -- dy -- | dist -- | lock --");
        lv_obj_set_size(s_dock, UI_TELEMETRY_W, UI_TELEMETRY_H);
        app_ui_label_set_dots(s_dock);
        lv_obj_set_style_text_align(s_dock, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 56, -18);
    }
    if (s_hint == NULL)
    {
        s_hint = lv_label_create(s_top_bar != NULL ? s_top_bar : scr);
        app_ui_style_top_label(s_hint, lv_color_hex(0x7DD3FC));
        lv_obj_set_width(s_hint, UI_HUD_HINT_W);
        app_ui_label_set_dots(s_hint);
        lv_label_set_text(s_hint, "接驳等待");
        lv_obj_set_pos(s_hint, 458, 11);
    }
    if (s_cap_btn == NULL)
    {
        s_cap_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_cap_btn, lv_color_hex(0x177A58));
        app_ui_add_button_label(s_cap_btn, "CAP");
        lv_obj_align(s_cap_btn, LV_ALIGN_RIGHT_MID, -14, 68);
        lv_obj_add_event_cb(s_cap_btn, app_ui_capture_start_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_flag(s_cap_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_stop_btn == NULL)
    {
        s_stop_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_stop_btn, lv_color_hex(0x8A3030));
        app_ui_add_button_label(s_stop_btn, "STOP");
        lv_obj_align(s_stop_btn, LV_ALIGN_RIGHT_MID, -14, 118);
        lv_obj_add_event_cb(s_stop_btn, app_ui_capture_stop_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_flag(s_stop_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_mode_btn == NULL)
    {
        s_mode_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_mode_btn, lv_color_hex(0x2F5D88));
        s_mode_label = app_ui_add_button_label(s_mode_btn,
            app_ai_capture_mode_label(app_ai_capture_get_mode()));
        lv_obj_align(s_mode_btn, LV_ALIGN_RIGHT_MID, -14, 18);
        lv_obj_add_event_cb(s_mode_btn, app_ui_capture_mode_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_flag(s_mode_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_capture == NULL)
    {
        s_capture = lv_label_create(scr);
        app_ui_style_hint_label(s_capture);
        lv_label_set_text(s_capture, "cap:DRONE off #0");
        lv_obj_set_width(s_capture, 132);
        lv_obj_set_style_text_align(s_capture, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_capture, LV_ALIGN_RIGHT_MID, -14, 166);
        lv_obj_add_flag(s_capture, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_weather_guard_btn == NULL)
    {
        s_weather_guard_btn = app_ui_button_create(scr);
        app_ui_style_weather_guard_button(s_weather_guard_btn);
        lv_obj_t *weather_label = app_ui_add_button_label(s_weather_guard_btn, "模拟天气");
        lv_obj_set_width(weather_label, 108);
        lv_obj_set_style_text_font(weather_label, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(weather_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(s_weather_guard_btn, app_ui_weather_emergency_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_align(s_weather_guard_btn, LV_ALIGN_BOTTOM_RIGHT, -18, -18);
    }
    app_ui_set_hidden(s_mode_btn, true);
    app_ui_set_hidden(s_cap_btn, true);
    app_ui_set_hidden(s_stop_btn, true);
    app_ui_set_hidden(s_capture, true);
    app_ui_set_hidden(s_top_bar, true);
    app_ui_set_hidden(s_dock, true);
    app_ui_set_hidden(s_weather_guard_btn, false);

    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++)
    {
        app_ui_set_hidden(s_lock_seg[i], true);
    }

    app_ui_move_foreground(s_hud_layer);
    app_ui_move_foreground(s_status);
    app_ui_move_foreground(s_vision);
    app_ui_move_foreground(s_coord);
    app_ui_move_foreground(s_hint);
    app_ui_move_foreground(s_flow_panel);
    app_ui_move_foreground(s_auth);
    app_ui_move_foreground(s_mode_btn);
    app_ui_move_foreground(s_cap_btn);
    app_ui_move_foreground(s_stop_btn);
    app_ui_move_foreground(s_capture);
    app_ui_move_foreground(s_weather_guard_btn);
    bsp_display_unlock();
    return true;
}
// 创建/显示启动页，包含 logo、进度条、队名和竞赛名称。
bool app_ui_show_loading(void)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }
    lv_obj_t *scr = app_ui_get_active_screen();
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
    }
    lv_obj_move_foreground(s_loading_layer);
    lv_refr_now(NULL);
    bsp_display_unlock();
    return true;
}
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
// 删除启动页对象，让相机 canvas 和 HUD 成为主显示层。
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
void app_ui_set_weather_emergency_callback(app_ui_weather_emergency_cb_t cb)
{
    s_weather_emergency_cb = cb;
}
// 天气按钮事件，真正的保护动作由 main 注册的回调执行。
static void app_ui_weather_emergency_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (s_weather_emergency_cb != NULL)
    {
        s_weather_emergency_cb();
    }
}
void app_ui_set_status(const char *text)
{
    if ((text == NULL) || (s_status == NULL && s_flow_title == NULL))
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    const char *status_display = app_ui_status_display_text(text, NULL);
    app_ui_set_label_text_unlocked(s_status, status_display);
    app_ui_set_label_text_unlocked(s_hint, "接驳等待");
    app_ui_flow_snapshot_t flow = {0};
    app_ui_flow_snapshot_default(status_display, &flow);
    app_ui_update_flow_unlocked(&flow);
    s_control_cache_valid = false;
    bsp_display_unlock();
}
// 更新视觉框、准星颜色、锁定条和认证提示；调用者需持有 LVGL 锁。
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
        // 视觉短时丢失时保留上一帧框，但降低透明度提示这是 hold 状态。
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
    char track_label[24] = {0};
    app_ui_format_track_label(dock, hold_box, track_label, sizeof(track_label));
    app_ui_set_track_box(show_box, box_x, box_y, box_w, box_h, box_color, box_opa, track_label);
    if ((s_cross_h != NULL) && (s_cross_v != NULL))
    {
        lv_color_t cross_color = app_ui_state_color(dock->state, false);
        lv_obj_set_style_bg_color(s_cross_h, cross_color, 0);
        lv_obj_set_style_bg_color(s_cross_v, cross_color, 0);
        lv_obj_set_style_bg_opa(s_cross_h,
            dock->state == APP_DOCK_STATE_SEARCHING ? (lv_opa_t)90 : (lv_opa_t)150,
            0);
        lv_obj_set_style_bg_opa(s_cross_v,
            dock->state == APP_DOCK_STATE_SEARCHING ? (lv_opa_t)90 : (lv_opa_t)150,
            0);
    }
    if (s_reticle_ring != NULL)
    {
        lv_color_t cross_color = app_ui_state_color(dock->state, false);
        lv_obj_set_style_border_color(s_reticle_ring, cross_color, 0);
        lv_obj_set_style_border_opa(s_reticle_ring,
            dock->state == APP_DOCK_STATE_SEARCHING ? (lv_opa_t)58 : (lv_opa_t)100,
            0);
    }
    app_ui_update_lock_bar(dock);
    app_ui_update_telemetry_unlocked(dock);
    app_ui_update_auth_banner(dock->state);
}
// 控制器一次性刷新状态栏、任务行、调试行和 HUD，减少多次抢 LVGL 锁。
void app_ui_update_control_state(const char *status,
    const char *vision_text,
    const char *dock_text,
    const app_vision_result_t *vision,
    const app_dock_judge_result_t *dock,
    const app_ui_flow_snapshot_t *flow)
{
    const char *status_display = app_ui_status_display_text(status, dock);
    const char *vision_display = app_ui_vision_display_text(vision_text, dock);
    app_ui_flow_snapshot_t fallback_flow = {0};
    if (flow == NULL)
    {
        app_ui_flow_snapshot_default(status_display, &fallback_flow);
        flow = &fallback_flow;
    }
    if (!app_ui_control_needs_update(status_display, vision_display, vision, dock, flow))
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_header_unlocked(status_display, vision_display, dock_text, dock);
    app_ui_update_flow_unlocked(flow);
    app_ui_update_hud_unlocked(vision, dock);
    app_ui_store_control_cache(status_display, vision_display, vision, dock, flow);
    bsp_display_unlock();
}
