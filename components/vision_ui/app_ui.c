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
LV_FONT_DECLARE(font_button_cn)
LV_FONT_DECLARE(font_safety_title_cn)
LV_FONT_DECLARE(font_hud_cn)
LV_FONT_DECLARE(font_title_en)
LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk)
LV_IMAGE_DECLARE(logo);

#define HUD_SRC_W               320
#define HUD_SRC_H               240
#define HUD_LOCK_SEG_COUNT      2
#define HUD_AUTH_SHOW_MS        1200
#define UI_LOCK_SHORT_MS        30
#define UI_LOCK_BOOT_MS         300
#define UI_TOP_BAR_X            264
#define UI_TOP_BAR_Y            18
#define UI_TOP_BAR_W            (BSP_LCD_H_RES - UI_TOP_BAR_X - 18)
#define UI_TOP_BAR_H            52
#define UI_HUD_STATUS_W         260
#define UI_HUD_VISION_W         220
#define UI_HUD_HINT_W           190
#define UI_FLOW_X               16
#define UI_FLOW_Y               18
#define UI_FLOW_W               228
#define UI_FLOW_H               252
#define UI_FLOW_TITLE_W         204
#define UI_FLOW_TITLE_H         34
#define UI_FLOW_RULE_W          196
#define UI_FLOW_STEP_TOP        56
#define UI_FLOW_STEP_W          204
#define UI_FLOW_STEP_H          40
#define UI_FLOW_STEP_PITCH      44
#define UI_FLOW_DOT_X           14
#define UI_FLOW_DOT_Y           14
#define UI_FLOW_DOT_SIZE        12
#define UI_FLOW_LINE_W          2
#define UI_FLOW_TEXT_X          38
#define UI_FLOW_NAME_W          78
#define UI_FLOW_DETAIL_X        118
#define UI_FLOW_DETAIL_W        78
#define UI_FLOW_LABEL_Y         8
#define UI_FLOW_ACTIVE_BAR_W    3
#define UI_FLOW_ACTIVE_BAR_H    28
#define UI_RETICLE_SIZE         72
#define UI_TELEMETRY_W          620
#define UI_TELEMETRY_H          40
#define UI_TELEMETRY_BOTTOM_MARGIN 44
#define UI_TASK_INTRO_W         620
#define UI_TASK_INTRO_H         320
#define UI_TASK_INTRO_CONTENT_W (UI_TASK_INTRO_W - 112)
#define UI_TASK_INTRO_STEP_W    152
#define UI_TASK_INTRO_STEP_H    54
#define UI_TRACK_CORNER_COUNT   8
#define UI_TRACK_LABEL_W        118
#define UI_TRACK_LABEL_H        24
#define UI_CONTEST_NAME         "第九届（2026）全国大学生嵌入式芯片与系统设计竞赛"
#define UI_TEAM_NAME            "地线引力队"
#define UI_SAFETY_MARGIN        24
#define UI_SAFETY_TOP_Y         16
#define UI_SAFETY_TOP_W         (BSP_LCD_H_RES - (UI_SAFETY_MARGIN * 2))
#define UI_SAFETY_TOP_H         54
#define UI_SAFETY_BOTTOM_W      (BSP_LCD_H_RES - (UI_SAFETY_MARGIN * 2))
#define UI_SAFETY_BOTTOM_H      76
#define UI_SAFETY_BOTTOM_Y      (BSP_LCD_V_RES - UI_SAFETY_BOTTOM_H - 18)
#define UI_SAFETY_ALERT_W       540
#define UI_SAFETY_ALERT_H       178
#define UI_SAFETY_BUTTON_W      148
#define UI_SAFETY_BUTTON_H      44
#define UI_SAFETY_ALERT_FLASH_MS 1500U

static const char *TAG = "app_ui";

// HUD、流程面板和启动页对象。
static lv_obj_t *s_top_bar = NULL;
static lv_obj_t *s_status = NULL;
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
static lv_obj_t *s_task_intro_layer = NULL;
static lv_obj_t *s_task_intro_target = NULL;
static lv_obj_t *s_safety_layer = NULL;
static lv_obj_t *s_safety_banner = NULL;
static lv_obj_t *s_safety_top_line = NULL;
static lv_obj_t *s_safety_title = NULL;
static lv_obj_t *s_safety_phase = NULL;
static lv_obj_t *s_safety_target = NULL;
static lv_obj_t *s_safety_alert = NULL;
static lv_obj_t *s_safety_alert_title = NULL;
static lv_obj_t *s_safety_alert_detail = NULL;
static lv_obj_t *s_safety_count = NULL;
static lv_obj_t *s_safety_status = NULL;
static lv_obj_t *s_safety_status_dot = NULL;
static lv_obj_t *s_safety_status_title = NULL;
static lv_obj_t *s_safety_status_detail = NULL;
static lv_obj_t *s_safety_typhoon_btn = NULL;
static lv_obj_t *s_safety_typhoon_label = NULL;
static app_ui_safety_typhoon_cb_t s_safety_typhoon_cb = NULL;
static lv_timer_t *s_safety_alert_timer = NULL;
static app_ui_safety_takeover_state_t s_safety_alert_state = APP_UI_SAFETY_TAKEOVER_IDLE;
static bool s_safety_alert_flash_done = false;

static void app_ui_safety_typhoon_event_cb(lv_event_t *e);

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
static char s_control_status_text[64] = {0};
static char s_control_vision_text[64] = {0};
static char s_control_dock_text[160] = {0};
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
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)242, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xC7DCE3), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 14, 0);
    lv_obj_set_style_shadow_offset_y(obj, 3, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x8AA6AF), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)46, 0);
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
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)242, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xC7DCE3), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 16, 0);
    lv_obj_set_style_shadow_offset_y(obj, 4, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x8AA6AF), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)48, 0);
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
    lv_obj_set_style_text_color(obj, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(obj, &font_main_title_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
}
static void app_ui_style_flow_rule(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x0F766E), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)176, 0);
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
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(obj, 8, 0);
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
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)150, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
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
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
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
    lv_obj_set_style_text_color(obj, primary ? lv_color_hex(0x334155) : lv_color_hex(0x64748B), 0);
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
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)232, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xB7E3EC), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(obj, 14, 0);
    lv_obj_set_style_pad_ver(obj, 7, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_shadow_width(obj, 12, 0);
    lv_obj_set_style_shadow_offset_y(obj, 3, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x8AA6AF), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)54, 0);
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
static void app_ui_style_task_intro_layer(lv_obj_t *obj)
{
    lv_obj_set_size(obj, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xE7F3F5), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xF9FCFD), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
static void app_ui_style_task_intro_panel(lv_obj_t *obj)
{
    lv_obj_set_size(obj, UI_TASK_INTRO_W, UI_TASK_INTRO_H);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xF8FCFD), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xB8DCE2), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 26, 0);
    lv_obj_set_style_shadow_offset_y(obj, 8, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x6D8D98), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)72, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
static void app_ui_style_task_intro_label(lv_obj_t *obj,
    lv_color_t color,
    lv_text_align_t align)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_text_font(obj, &font_loading_cn, 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
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

static void app_ui_style_safety_panel(lv_obj_t *obj,
    lv_color_t bg,
    lv_color_t border,
    lv_opa_t opa,
    int32_t radius)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 10, 0);
    lv_obj_set_style_shadow_offset_y(obj, 3, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)42, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void app_ui_safety_set_label(lv_obj_t *label, const char *text)
{
    if (label != NULL)
    {
        lv_label_set_text(label, text != NULL ? text : "");
    }
}

static void app_ui_safety_set_label_clip(lv_obj_t *label)
{
    if (label == NULL)
    {
        return;
    }
#if LVGL_VERSION_MAJOR >= 9
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
#else
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
#endif
}

static void app_ui_safety_set_hidden(lv_obj_t *obj, bool hidden)
{
    app_ui_set_hidden(obj, hidden);
}

static bool app_ui_safety_alert_should_flash(app_ui_safety_takeover_state_t state)
{
    return state == APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED ||
           state == APP_UI_SAFETY_TAKEOVER_TYPHOON ||
           state == APP_UI_SAFETY_TAKEOVER_SAFE_DONE;
}

static void app_ui_safety_alert_timer_stop_unlocked(void)
{
    if (s_safety_alert_timer != NULL)
    {
        lv_timer_del(s_safety_alert_timer);
        s_safety_alert_timer = NULL;
    }
}

static void app_ui_safety_alert_timer_cb(lv_timer_t *timer)
{
    if (timer == s_safety_alert_timer)
    {
        s_safety_alert_timer = NULL;
    }
    s_safety_alert_flash_done = true;
    app_ui_safety_set_hidden(s_safety_alert, true);
}

static void app_ui_safety_reset_alert_flash_unlocked(app_ui_safety_takeover_state_t state)
{
    if (s_safety_alert_state == state)
    {
        return;
    }
    app_ui_safety_alert_timer_stop_unlocked();
    s_safety_alert_state = state;
    s_safety_alert_flash_done = false;
}

static void app_ui_safety_apply_alert_visibility_unlocked(bool show_alert,
    app_ui_safety_takeover_state_t state)
{
    app_ui_safety_reset_alert_flash_unlocked(state);
    if (!show_alert)
    {
        app_ui_safety_alert_timer_stop_unlocked();
        app_ui_safety_set_hidden(s_safety_alert, true);
        return;
    }

    if (!app_ui_safety_alert_should_flash(state))
    {
        app_ui_safety_alert_timer_stop_unlocked();
        app_ui_safety_set_hidden(s_safety_alert, false);
        return;
    }

    if (s_safety_alert_flash_done)
    {
        app_ui_safety_set_hidden(s_safety_alert, true);
        return;
    }

    app_ui_safety_set_hidden(s_safety_alert, false);
    if (s_safety_alert_timer == NULL)
    {
        s_safety_alert_timer = lv_timer_create(app_ui_safety_alert_timer_cb,
            UI_SAFETY_ALERT_FLASH_MS,
            NULL);
        if (s_safety_alert_timer != NULL)
        {
            lv_timer_set_repeat_count(s_safety_alert_timer, 1);
        }
    }
}

static void app_ui_safety_format_target(char *buf, size_t buf_size, uint16_t target_id)
{
    if (buf == NULL || buf_size == 0)
    {
        return;
    }
    if (target_id != 0U)
    {
        snprintf(buf, buf_size, "TAG %u", (unsigned)target_id);
    }
    else
    {
        snprintf(buf, buf_size, "TAG --");
    }
}

static void app_ui_create_safety_takeover_unlocked(lv_obj_t *scr)
{
    if (s_safety_layer != NULL)
    {
        return;
    }

    s_safety_layer = lv_obj_create(scr);
    lv_obj_set_size(s_safety_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_opa(s_safety_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_safety_layer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_safety_layer, 0, 0);
    lv_obj_set_style_pad_all(s_safety_layer, 0, 0);
    lv_obj_clear_flag(s_safety_layer, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(s_safety_layer, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(s_safety_layer, LV_OBJ_FLAG_CLICKABLE);
#endif

    s_safety_banner = lv_obj_create(s_safety_layer);
    app_ui_style_safety_panel(s_safety_banner,
        lv_color_hex(0xFFFFFF),
        lv_color_hex(0x7DD3FC),
        (lv_opa_t)232,
        8);
    lv_obj_set_size(s_safety_banner, UI_SAFETY_TOP_W, UI_SAFETY_TOP_H);
    lv_obj_set_pos(s_safety_banner, UI_SAFETY_MARGIN, UI_SAFETY_TOP_Y);

    s_safety_top_line = lv_obj_create(s_safety_banner);
    lv_obj_set_size(s_safety_top_line, UI_SAFETY_TOP_W - 32, 2);
    lv_obj_set_style_bg_color(s_safety_top_line, lv_color_hex(0x0EA5E9), 0);
    lv_obj_set_style_bg_opa(s_safety_top_line, (lv_opa_t)210, 0);
    lv_obj_set_style_border_width(s_safety_top_line, 0, 0);
    lv_obj_set_style_radius(s_safety_top_line, 1, 0);
    lv_obj_set_style_pad_all(s_safety_top_line, 0, 0);
    lv_obj_clear_flag(s_safety_top_line, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(s_safety_top_line, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(s_safety_top_line, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_align(s_safety_top_line, LV_ALIGN_TOP_MID, 0, 0);

    s_safety_title = lv_label_create(s_safety_banner);
    lv_obj_set_width(s_safety_title, 220);
    app_ui_safety_set_label_clip(s_safety_title);
    lv_obj_set_style_text_color(s_safety_title, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(s_safety_title, &font_hud_cn, 0);
    lv_label_set_text(s_safety_title, "安全接管");
    lv_obj_align(s_safety_title, LV_ALIGN_LEFT_MID, 24, 1);

    s_safety_phase = lv_label_create(s_safety_banner);
    lv_obj_set_width(s_safety_phase, 420);
    app_ui_safety_set_label_clip(s_safety_phase);
    lv_obj_set_style_text_color(s_safety_phase, lv_color_hex(0x0369A1), 0);
    lv_obj_set_style_text_font(s_safety_phase, &font_loading_cn, 0);
    lv_obj_set_style_text_align(s_safety_phase, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_safety_phase, "任务就绪");
    lv_obj_align(s_safety_phase, LV_ALIGN_CENTER, 0, 1);

    s_safety_target = lv_label_create(s_safety_banner);
    lv_obj_set_width(s_safety_target, 150);
    app_ui_safety_set_label_clip(s_safety_target);
    lv_obj_set_style_text_color(s_safety_target, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(s_safety_target, &font_hud_cn, 0);
    lv_obj_set_style_text_align(s_safety_target, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_safety_target, "TAG --");
    lv_obj_align(s_safety_target, LV_ALIGN_RIGHT_MID, -24, 1);

    s_safety_status = lv_obj_create(s_safety_layer);
    app_ui_style_safety_panel(s_safety_status,
        lv_color_hex(0xFFFFFF),
        lv_color_hex(0xBAE6FD),
        (lv_opa_t)232,
        8);
    lv_obj_set_size(s_safety_status, UI_SAFETY_BOTTOM_W, UI_SAFETY_BOTTOM_H);
    lv_obj_set_pos(s_safety_status, UI_SAFETY_MARGIN, UI_SAFETY_BOTTOM_Y);

    s_safety_status_dot = lv_obj_create(s_safety_status);
    lv_obj_set_size(s_safety_status_dot, 10, 10);
    lv_obj_set_style_bg_color(s_safety_status_dot, lv_color_hex(0x0EA5E9), 0);
    lv_obj_set_style_bg_opa(s_safety_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_safety_status_dot, 0, 0);
    lv_obj_set_style_radius(s_safety_status_dot, 5, 0);
    lv_obj_set_style_pad_all(s_safety_status_dot, 0, 0);
    lv_obj_clear_flag(s_safety_status_dot, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(s_safety_status_dot, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(s_safety_status_dot, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_align(s_safety_status_dot, LV_ALIGN_LEFT_MID, 24, 0);

    s_safety_status_title = lv_label_create(s_safety_status);
    lv_obj_set_width(s_safety_status_title, UI_SAFETY_BOTTOM_W - UI_SAFETY_BUTTON_W - 112);
    app_ui_safety_set_label_clip(s_safety_status_title);
    lv_obj_set_style_text_color(s_safety_status_title, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(s_safety_status_title, &font_loading_cn, 0);
    lv_label_set_text(s_safety_status_title, "正在识别无人机");
    lv_obj_align(s_safety_status_title, LV_ALIGN_TOP_LEFT, 46, 10);

    s_safety_status_detail = lv_label_create(s_safety_status);
    lv_obj_set_width(s_safety_status_detail, UI_SAFETY_BOTTOM_W - UI_SAFETY_BUTTON_W - 112);
    app_ui_safety_set_label_clip(s_safety_status_detail);
    lv_obj_set_style_text_color(s_safety_status_detail, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(s_safety_status_detail, &font_hud_cn, 0);
    lv_label_set_text(s_safety_status_detail, "请将目标无人机移入画面");
    lv_obj_align(s_safety_status_detail, LV_ALIGN_TOP_LEFT, 46, 43);

    s_safety_typhoon_btn = app_ui_button_create(s_safety_status);
    lv_obj_set_size(s_safety_typhoon_btn, UI_SAFETY_BUTTON_W, UI_SAFETY_BUTTON_H);
    lv_obj_set_style_bg_color(s_safety_typhoon_btn, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_opa(s_safety_typhoon_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_safety_typhoon_btn, 1, 0);
    lv_obj_set_style_border_color(s_safety_typhoon_btn, lv_color_hex(0xFEE2E2), 0);
    lv_obj_set_style_radius(s_safety_typhoon_btn, 8, 0);
    lv_obj_set_style_pad_all(s_safety_typhoon_btn, 0, 0);
    lv_obj_clear_flag(s_safety_typhoon_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_safety_typhoon_btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(s_safety_typhoon_btn, app_ui_safety_typhoon_event_cb, LV_EVENT_CLICKED, NULL);

    s_safety_typhoon_label = lv_label_create(s_safety_typhoon_btn);
    lv_obj_set_width(s_safety_typhoon_label, UI_SAFETY_BUTTON_W - 16);
    lv_obj_set_style_text_color(s_safety_typhoon_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_safety_typhoon_label, &font_hud_cn, 0);
    lv_obj_set_style_text_align(s_safety_typhoon_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_safety_typhoon_label, "台风预警");
    lv_obj_center(s_safety_typhoon_label);

    s_safety_alert = lv_obj_create(s_safety_layer);
    app_ui_style_safety_panel(s_safety_alert,
        lv_color_hex(0xFFFFFF),
        lv_color_hex(0xBAE6FD),
        (lv_opa_t)242,
        8);
    lv_obj_set_size(s_safety_alert, UI_SAFETY_ALERT_W, UI_SAFETY_ALERT_H);
    lv_obj_align(s_safety_alert, LV_ALIGN_CENTER, 0, -4);

    s_safety_alert_title = lv_label_create(s_safety_alert);
    lv_obj_set_width(s_safety_alert_title, UI_SAFETY_ALERT_W - 64);
    app_ui_safety_set_label_clip(s_safety_alert_title);
    lv_obj_set_style_text_color(s_safety_alert_title, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(s_safety_alert_title, &font_safety_title_cn, 0);
    lv_obj_set_style_text_align(s_safety_alert_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_safety_alert_title, "未检测到无人机");
    lv_obj_align(s_safety_alert_title, LV_ALIGN_TOP_MID, 0, 44);

    s_safety_alert_detail = lv_label_create(s_safety_alert);
    lv_obj_set_width(s_safety_alert_detail, UI_SAFETY_ALERT_W - 72);
    app_ui_safety_set_label_clip(s_safety_alert_detail);
    lv_obj_set_style_text_color(s_safety_alert_detail, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(s_safety_alert_detail, &font_loading_cn, 0);
    lv_obj_set_style_text_align(s_safety_alert_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_safety_alert_detail, "接驳保持开启");
    lv_obj_align(s_safety_alert_detail, LV_ALIGN_TOP_MID, 0, 94);

    s_safety_count = lv_label_create(s_safety_alert);
    lv_obj_set_width(s_safety_count, UI_SAFETY_ALERT_W - 80);
    app_ui_safety_set_label_clip(s_safety_count);
    lv_obj_set_style_text_color(s_safety_count, lv_color_hex(0xD97706), 0);
    lv_obj_set_style_text_font(s_safety_count, &font_loading_cn, 0);
    lv_obj_set_style_text_align(s_safety_count, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_safety_count, "");
    lv_obj_align(s_safety_count, LV_ALIGN_BOTTOM_MID, 0, -22);

    lv_obj_add_flag(s_safety_alert, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_safety_layer, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *app_ui_create_task_intro_step(lv_obj_t *parent,
    const char *title,
    const char *detail,
    int32_t x,
    int32_t y,
    lv_color_t accent,
    lv_color_t fill)
{
    lv_obj_t *step = lv_obj_create(parent);
    lv_obj_set_size(step, UI_TASK_INTRO_STEP_W, UI_TASK_INTRO_STEP_H);
    lv_obj_set_style_bg_color(step, fill, 0);
    lv_obj_set_style_bg_opa(step, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(step, 1, 0);
    lv_obj_set_style_border_color(step, accent, 0);
    lv_obj_set_style_border_opa(step, (lv_opa_t)120, 0);
    lv_obj_set_style_radius(step, 10, 0);
    lv_obj_set_style_pad_all(step, 0, 0);
    lv_obj_clear_flag(step, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(step, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(step, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_align(step, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *dot = lv_obj_create(step);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_set_style_bg_color(dot, accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 5, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 16, 14);

    lv_obj_t *title_label = lv_label_create(step);
    app_ui_style_task_intro_label(title_label, lv_color_hex(0x334155), LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(title_label, 94);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 33, 8);

    lv_obj_t *detail_label = lv_label_create(step);
    app_ui_style_task_intro_label(detail_label, accent, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(detail_label, 118);
    lv_label_set_text(detail_label, detail);
    lv_obj_align(detail_label, LV_ALIGN_TOP_LEFT, 33, 29);
    return step;
}

static void app_ui_set_task_intro_target_unlocked(uint16_t target_id)
{
    if (s_task_intro_target == NULL)
    {
        return;
    }
    char target_buf[32] = {0};
    if (target_id != 0U)
    {
        snprintf(target_buf, sizeof(target_buf), "TAG %u", (unsigned)target_id);
    }
    else
    {
        snprintf(target_buf, sizeof(target_buf), "TAG --");
    }
    lv_label_set_text(s_task_intro_target, target_buf);
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
static void app_ui_set_top_chip_text_unlocked(lv_obj_t *label,
    const char *prefix,
    const char *value)
{
    if (label == NULL)
    {
        return;
    }
    char buf[64] = {0};
    snprintf(buf,
        sizeof(buf),
        "%s：%s",
        (prefix != NULL && prefix[0] != '\0') ? prefix : "状态",
        (value != NULL && value[0] != '\0') ? value : "等待");
    app_ui_set_label_text_unlocked(label, buf);
}
static const char *app_ui_flow_step_title(app_ui_flow_step_t step)
{
    switch (step)
    {
    case APP_UI_FLOW_STEP_DRONE:
        return "无人机";
    case APP_UI_FLOW_STEP_TAG:
        return "\u6807\u7B7E";
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
        return lv_color_hex(0x2563EB);
    case APP_UI_FLOW_STATE_DONE:
        return lv_color_hex(0x0F766E);
    case APP_UI_FLOW_STATE_ERROR:
        return lv_color_hex(0xB91C1C);
    case APP_UI_FLOW_STATE_WAITING:
    default:
        return lv_color_hex(0x64748B);
    }
}

static const char *app_ui_flow_headline_display_text(const char *headline)
{
    if (headline == NULL || headline[0] == '\0')
    {
        return "当前：等待任务";
    }
    if (strcmp(headline, "等待云端") == 0 || strcmp(headline, "系统启动") == 0)
    {
        return "当前：等待任务";
    }
    if (strstr(headline, "AI") != NULL)
    {
        return "当前：识别无人机";
    }
    if (strstr(headline, "Tag") != NULL)
    {
        return "当前：校验标签";
    }
    if (strcmp(headline, "接驳") == 0 ||
        strcmp(headline, "取货") == 0 ||
        strcmp(headline, "开") == 0 ||
        strcmp(headline, "闭") == 0 ||
        strcmp(headline, "已开") == 0)
    {
        return "当前：自动接驳";
    }
    if (strcmp(headline, "接驳完成") == 0)
    {
        return "当前：接驳完成";
    }
    if (strcmp(headline, "接驳ERR") == 0)
    {
        return "当前：接驳异常";
    }
    if (strcmp(headline, "接驳OK") == 0)
    {
        return "当前：接驳完成";
    }
    if (strcmp(headline, "STOP") == 0)
    {
        return "当前：任务停止";
    }
    return headline;
}

static const char *app_ui_flow_detail_display_text(app_ui_flow_step_t step, const char *detail)
{
    if (detail == NULL || detail[0] == '\0')
    {
        return "等待";
    }
    if (strcmp(detail, "AI OK") == 0)
    {
        return "已识别";
    }
    if (strstr(detail, "AI ") == detail)
    {
        return "识别中";
    }
    if (strcmp(detail, "Tag OK") == 0)
    {
        return "已通过";
    }
    if (strstr(detail, "Tag ") == detail)
    {
        return "已发现";
    }
    if (strcmp(detail, "等待Tag") == 0)
    {
        return "等待";
    }
    if (strcmp(detail, "READY") == 0)
    {
        return "可接驳";
    }
    if (strcmp(detail, "STOP") == 0)
    {
        return "已停止";
    }
    if (strcmp(detail, "开") == 0)
    {
        return "开门中";
    }
    if (strcmp(detail, "已开") == 0)
    {
        return "门已开";
    }
    if (strcmp(detail, "闭") == 0)
    {
        return "关门中";
    }
    if (strcmp(detail, "取货OK") == 0)
    {
        return "取货完成";
    }
    if (strcmp(detail, "接驳") == 0 && step == APP_UI_FLOW_STEP_EXEC)
    {
        return "执行中";
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
        return "异常";
    }
    if (strcmp(detail, "接驳OK") == 0)
    {
        return "完成";
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
    const lv_color_t fill = (state == APP_UI_FLOW_STATE_ERROR) ? lv_color_hex(0xFEF2F2) :
        (state == APP_UI_FLOW_STATE_DONE) ? lv_color_hex(0xECFDF5) :
        active ? lv_color_hex(0xEFF6FF) : lv_color_hex(0xFFFFFF);
    lv_obj_set_style_bg_color(s_flow_step[index],
        fill,
        0);
    lv_obj_set_style_bg_opa(s_flow_step[index],
        (active || state == APP_UI_FLOW_STATE_DONE || state == APP_UI_FLOW_STATE_ERROR) ?
            (lv_opa_t)220 : LV_OPA_TRANSP,
        0);
    lv_obj_set_style_border_width(s_flow_step[index], 1, 0);
    lv_obj_set_style_border_color(s_flow_step[index], color, 0);
    lv_obj_set_style_border_opa(s_flow_step[index],
        (active || state == APP_UI_FLOW_STATE_DONE || state == APP_UI_FLOW_STATE_ERROR) ?
            (lv_opa_t)92 : LV_OPA_TRANSP,
        0);
    if (s_flow_step_dot[index] != NULL)
    {
        lv_obj_set_style_bg_color(s_flow_step_dot[index], color, 0);
        lv_obj_set_style_bg_opa(s_flow_step_dot[index],
            active ? LV_OPA_COVER :
            (state == APP_UI_FLOW_STATE_WAITING ? (lv_opa_t)116 : (lv_opa_t)220),
            0);
        lv_obj_set_style_border_color(s_flow_step_dot[index],
            active ? lv_color_hex(0xFFFFFF) : color,
            0);
        lv_obj_set_style_border_opa(s_flow_step_dot[index],
            active ? LV_OPA_COVER : (lv_opa_t)140,
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
            completed_line ? lv_color_hex(0x0F766E) : lv_color_hex(0xCBD5E1),
            0);
        lv_obj_set_style_bg_opa(s_flow_step_line[index],
            completed_line ? (lv_opa_t)210 : LV_OPA_COVER,
            0);
    }
    if (s_flow_step_name[index] != NULL)
    {
        lv_obj_set_style_text_color(s_flow_step_name[index],
            (state == APP_UI_FLOW_STATE_ERROR) ? color :
            active ? lv_color_hex(0x0F172A) :
            (state == APP_UI_FLOW_STATE_WAITING ? lv_color_hex(0x475569) : color),
            0);
        lv_obj_set_style_text_font(s_flow_step_name[index], &font_loading_cn, 0);
    }
    if (s_flow_step_detail[index] != NULL)
    {
        lv_obj_set_style_text_color(s_flow_step_detail[index],
            (state == APP_UI_FLOW_STATE_ERROR) ? color :
            active ? color :
            (state == APP_UI_FLOW_STATE_DONE ? lv_color_hex(0x0F766E) : lv_color_hex(0x64748B)),
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
        if ((int)flow->active_step >= 0 && flow->active_step < APP_UI_FLOW_STEP_COUNT)
        {
            lv_obj_set_style_text_color(s_flow_title,
                app_ui_flow_state_color(flow->step_state[flow->active_step]),
                0);
        }
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

static bool app_ui_text_is_display_ready(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return false;
    }
    return !app_ui_text_has(text, "task:") &&
           !app_ui_text_has(text, "dock:") &&
           !app_ui_text_has(text, "dock dbg:") &&
           !app_ui_text_has(text, "ai:") &&
           !app_ui_text_has(text, "wait_approach") &&
           !app_ui_text_has(text, "target=");
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
        return "标签异常";
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
        return "等待识别";
    }
    switch (dock->state) {
    case APP_DOCK_STATE_WRONG_ID:
        return "标签异常";
    case APP_DOCK_STATE_TRACKING:
        return "正在对准";
    case APP_DOCK_STATE_ALIGNED:
    case APP_DOCK_STATE_READY_TO_DOCK:
        return "已对准";
    case APP_DOCK_STATE_SEARCHING:
    default:
        return dock->vision_valid ? "已看到画面" : "等待识别";
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
        app_ui_text_has(text, "init"))
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
    if (app_ui_text_has(text, "weather"))
    {
        return "\u5929\u6C14\u4FDD\u62A4";
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
    if (app_ui_text_is_display_ready(text))
    {
        return text;
    }
    return "云端接驳";
}
static const char *app_ui_vision_display_text(const char *text,
    const app_dock_judge_result_t *dock)
{
    if (app_ui_text_is_display_ready(text))
    {
        return text;
    }
    if (dock != NULL)
    {
        return app_ui_vision_text_from_dock(dock);
    }
    if ((text == NULL) || (text[0] == '\0'))
    {
        return "等待识别";
    }
    return "等待识别";
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
    if (app_ui_text_is_display_ready(dock_text))
    {
        if (app_ui_text_has(dock_text, "\u63A5\u9A73\u6267\u884C"))
        {
            snprintf(out, out_size, "\u6267\u884C\u4E2D");
            return;
        }
        if (app_ui_text_has(dock_text, "\u65E0\u4EBA\u673A\u8BC6\u522B"))
        {
            snprintf(out, out_size, "\u7B49\u5F85Tag");
            return;
        }
        if (app_ui_text_has(dock_text, "\u8EAB\u4EFD\u901A\u8FC7"))
        {
            snprintf(out, out_size, "\u5DF2\u901A\u8FC7");
            return;
        }
        if (app_ui_text_has(dock_text, "\u641C\u7D22"))
        {
            snprintf(out, out_size, "\u641C\u7D22Tag");
            return;
        }
        if (app_ui_text_has(dock_text, "\u504F\u79FB") ||
            app_ui_text_has(dock_text, "\u5BF9\u51C6"))
        {
            snprintf(out, out_size, "\u5BF9\u51C6\u4E2D");
            return;
        }
        if (app_ui_text_has(dock_text, "\u7A33\u5B9A\u5E27"))
        {
            snprintf(out, out_size, "\u7A33\u5B9A\u4E2D");
            return;
        }
        if (strlen(dock_text) <= 36U)
        {
            snprintf(out, out_size, "%s", dock_text);
            return;
        }
    }
    if (dock != NULL)
    {
        int32_t distance_cm = app_ui_distance_cm_from_dock(dock);
        switch (dock->state) {
        case APP_DOCK_STATE_WRONG_ID:
            snprintf(out, out_size, "标签异常");
            return;
        case APP_DOCK_STATE_TRACKING:
            if (distance_cm > 0)
            {
                snprintf(out, out_size, "%ldcm 对准中", (long)distance_cm);
            }
            else
            {
                snprintf(out, out_size, "正在对准");
            }
            return;
        case APP_DOCK_STATE_ALIGNED:
            snprintf(out, out_size, "已对准");
            return;
        case APP_DOCK_STATE_READY_TO_DOCK:
            snprintf(out, out_size, "可接驳");
            return;
        case APP_DOCK_STATE_SEARCHING:
        default:
            snprintf(out, out_size, "等待识别");
            return;
        }
    }
    if (dock_text != NULL && dock_text[0] != '\0')
    {
        if (app_ui_text_has(dock_text, "err") || app_ui_text_has(dock_text, "ERR"))
        {
            snprintf(out, out_size, "异常");
        }
        else if (app_ui_text_has(dock_text, "ready") || app_ui_text_has(dock_text, "READY"))
        {
            snprintf(out, out_size, "可接驳");
        }
        else
        {
            snprintf(out, out_size, "等待识别");
        }
        return;
    }
    snprintf(out, out_size, "等待识别");
}
static void app_ui_set_header_unlocked(const char *status_text,
    const char *vision_text,
    const char *dock_text,
    const app_dock_judge_result_t *dock)
{
    char dock_buf[48] = {0};
    app_ui_set_top_chip_text_unlocked(s_status,
        "任务",
        (status_text != NULL && status_text[0] != '\0') ? status_text : "系统就绪");
    app_ui_set_top_chip_text_unlocked(s_vision,
        "视觉",
        (vision_text != NULL && vision_text[0] != '\0') ? vision_text : "等待识别");
    app_ui_format_header_dock(dock_text, dock, dock_buf, sizeof(dock_buf));
    app_ui_set_top_chip_text_unlocked(s_hint, "标签", dock_buf);
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
        snprintf(out, out_size, "保持");
        return;
    }
    if (dock == NULL)
    {
        snprintf(out, out_size, "标签");
        return;
    }
    int32_t distance_cm = app_ui_distance_cm_from_dock(dock);
    switch (dock->state) {
    case APP_DOCK_STATE_READY_TO_DOCK:
        snprintf(out, out_size, "可接驳");
        break;
    case APP_DOCK_STATE_WRONG_ID:
        snprintf(out, out_size, "标签异常");
        break;
    case APP_DOCK_STATE_TRACKING:
    case APP_DOCK_STATE_ALIGNED:
        if (distance_cm > 0)
        {
            snprintf(out, out_size, "%ldcm", (long)distance_cm);
        }
        else
        {
            snprintf(out, out_size, "标签%u", (unsigned)dock->tag_id);
        }
        break;
    case APP_DOCK_STATE_SEARCHING:
    default:
        snprintf(out, out_size, "标签%u", (unsigned)dock->tag_id);
        break;
    }
}
static void app_ui_format_telemetry(const char *dock_text,
    const app_dock_judge_result_t *dock,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0)
    {
        return;
    }
    if (app_ui_text_is_display_ready(dock_text))
    {
        snprintf(out, out_size, "%s", dock_text);
        return;
    }
    if (dock == NULL)
    {
        snprintf(out, out_size, "等无人机 标签-- 偏移-- 距离-- 稳定--");
        return;
    }
    int32_t distance_cm = app_ui_distance_cm_from_dock(dock);
    const char *phase = "等无人机";
    switch (dock->state) {
    case APP_DOCK_STATE_WRONG_ID:
        phase = "标签异常";
        break;
    case APP_DOCK_STATE_TRACKING:
        phase = "对准中";
        break;
    case APP_DOCK_STATE_ALIGNED:
        phase = "已对准";
        break;
    case APP_DOCK_STATE_READY_TO_DOCK:
        phase = "可接驳";
        break;
    case APP_DOCK_STATE_SEARCHING:
    default:
        phase = dock->vision_valid ? "等标签" : "等无人机";
        break;
    }
    if (distance_cm > 0)
    {
        snprintf(out,
            out_size,
            "%s 标签%u 偏移%+ld,%+ld 距离%ldcm 稳定帧%u",
            phase,
            (unsigned)dock->tag_id,
            (long)dock->dx,
            (long)dock->dy,
            (long)distance_cm,
            (unsigned)dock->stable_count);
    }
    else
    {
        snprintf(out,
            out_size,
            "%s 标签%u 偏移%+ld,%+ld 距离-- 稳定帧%u",
            phase,
            (unsigned)dock->tag_id,
            (long)dock->dx,
            (long)dock->dy,
            (unsigned)dock->stable_count);
    }
}
static void app_ui_update_telemetry_unlocked(const char *dock_text,
    const app_dock_judge_result_t *dock)
{
    char text[112] = {0};
    app_ui_format_telemetry(dock_text, dock, text, sizeof(text));
    app_ui_set_label_text_unlocked(s_dock, text);
}
static void app_ui_set_vision_text_unlocked(const char *text)
{
    app_ui_set_top_chip_text_unlocked(s_vision,
        "视觉",
        (text != NULL && text[0] != '\0') ? text : "等待识别");
}
static bool app_ui_control_needs_update(const char *status_text,
    const char *vision_text,
    const char *dock_text,
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
    if ((dock_text != NULL) && (strcmp(s_control_dock_text, dock_text) != 0))
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
    const char *dock_text,
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
    snprintf(s_control_dock_text,
        sizeof(s_control_dock_text),
        "%s",
        dock_text != NULL ? dock_text : "");
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
    app_ui_set_vision_text_unlocked("\u6293\u56FE\u542F\u52A8");
    esp_err_t ret = app_ai_capture_start();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "CAP start failed: %s", esp_err_to_name(ret));
        app_ui_set_vision_text_unlocked("\u6293\u56FE\u5931\u8D25");
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
static void app_ui_safety_typhoon_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED &&
        lv_event_get_code(e) != LV_EVENT_PRESSED)
    {
        return;
    }
    if (s_safety_typhoon_cb != NULL)
    {
        s_safety_typhoon_cb();
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
    app_ui_create_safety_takeover_unlocked(scr);
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
        lv_label_set_text(s_track_label, "\u6807\u7B7E");
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
    const int32_t y = UI_TOP_BAR_Y + 20;
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
        lv_label_set_text(s_flow_title, "当前：等待任务");
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
        app_ui_style_top_label(s_status, lv_color_hex(0x0F172A));
        lv_obj_set_width(s_status, UI_HUD_STATUS_W);
        app_ui_label_set_dots(s_status);
        lv_label_set_text(s_status, "任务：系统启动");
        lv_obj_set_pos(s_status, 18, 13);
    }
    if (s_vision == NULL)
    {
        s_vision = lv_label_create(s_top_bar != NULL ? s_top_bar : scr);
        app_ui_style_top_label(s_vision, lv_color_hex(0x0F766E));
        lv_obj_set_size(s_vision, UI_HUD_VISION_W, 30);
        app_ui_label_set_dots(s_vision);
        lv_label_set_text(s_vision, "视觉：等待识别");
        lv_obj_set_pos(s_vision, 300, 13);
    }
    if (s_dock == NULL)
    {
        s_dock = lv_label_create(scr);
        app_ui_style_telemetry_label(s_dock);
        lv_label_set_text(s_dock, "等无人机 标签-- 偏移-- 距离-- 稳定--");
        lv_obj_set_size(s_dock, UI_TELEMETRY_W, UI_TELEMETRY_H);
        app_ui_label_set_dots(s_dock);
        lv_obj_set_style_text_align(s_dock, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 38, -UI_TELEMETRY_BOTTOM_MARGIN);
    }
    if (s_hint == NULL)
    {
        s_hint = lv_label_create(s_top_bar != NULL ? s_top_bar : scr);
        app_ui_style_top_label(s_hint, lv_color_hex(0x2563EB));
        lv_obj_set_width(s_hint, UI_HUD_HINT_W);
        app_ui_label_set_dots(s_hint);
        lv_label_set_text(s_hint, "标签：等待识别");
        lv_obj_set_pos(s_hint, 526, 13);
    }
    if (s_cap_btn == NULL)
    {
        s_cap_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_cap_btn, lv_color_hex(0x177A58));
        app_ui_add_button_label(s_cap_btn, "\u6293\u56FE");
        lv_obj_align(s_cap_btn, LV_ALIGN_RIGHT_MID, -14, 68);
        lv_obj_add_event_cb(s_cap_btn, app_ui_capture_start_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_flag(s_cap_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_stop_btn == NULL)
    {
        s_stop_btn = app_ui_button_create(scr);
        app_ui_style_capture_button(s_stop_btn, lv_color_hex(0x8A3030));
        app_ui_add_button_label(s_stop_btn, "\u505C\u6B62");
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
        lv_label_set_text(s_capture, "\u6293\u56FE\u5173\u95ED #0");
        lv_obj_set_width(s_capture, 132);
        lv_obj_set_style_text_align(s_capture, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_capture, LV_ALIGN_RIGHT_MID, -14, 166);
        lv_obj_add_flag(s_capture, LV_OBJ_FLAG_HIDDEN);
    }
    app_ui_set_hidden(s_mode_btn, true);
    app_ui_set_hidden(s_cap_btn, true);
    app_ui_set_hidden(s_stop_btn, true);
    app_ui_set_hidden(s_capture, true);
    app_ui_set_hidden(s_hud_layer, true);
    app_ui_set_hidden(s_top_bar, true);
    app_ui_set_hidden(s_dock, true);
    app_ui_set_hidden(s_flow_panel, true);

    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++)
    {
        app_ui_set_hidden(s_lock_seg[i], true);
    }

    app_ui_move_foreground(s_hud_layer);
    app_ui_move_foreground(s_status);
    app_ui_move_foreground(s_vision);
    app_ui_move_foreground(s_hint);
    app_ui_move_foreground(s_flow_panel);
    app_ui_move_foreground(s_auth);
    app_ui_move_foreground(s_mode_btn);
    app_ui_move_foreground(s_cap_btn);
    app_ui_move_foreground(s_stop_btn);
    app_ui_move_foreground(s_capture);
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
// 远程任务进入预览前短暂停留，让评委先看到任务链路已启动。
bool app_ui_show_task_intro(uint16_t target_id)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }

    lv_obj_t *scr = app_ui_get_active_screen();
    if (s_task_intro_layer == NULL)
    {
        s_task_intro_layer = lv_obj_create(scr);
        app_ui_style_task_intro_layer(s_task_intro_layer);
        lv_obj_align(s_task_intro_layer, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *bg_line = lv_obj_create(s_task_intro_layer);
        lv_obj_set_size(bg_line, BSP_LCD_H_RES - 96, 2);
        lv_obj_set_style_bg_color(bg_line, lv_color_hex(0x79B6C2), 0);
        lv_obj_set_style_bg_opa(bg_line, (lv_opa_t)72, 0);
        lv_obj_set_style_border_width(bg_line, 0, 0);
        lv_obj_set_style_radius(bg_line, 1, 0);
        lv_obj_set_style_pad_all(bg_line, 0, 0);
        lv_obj_clear_flag(bg_line, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(bg_line, LV_ALIGN_CENTER, 0, 52);

        lv_obj_t *panel = lv_obj_create(s_task_intro_layer);
        app_ui_style_task_intro_panel(panel);
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *accent = lv_obj_create(panel);
        lv_obj_set_size(accent, UI_TASK_INTRO_CONTENT_W, 5);
        lv_obj_set_style_bg_color(accent, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_bg_grad_color(accent, lv_color_hex(0x2563EB), 0);
        lv_obj_set_style_bg_grad_dir(accent, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_pad_all(accent, 0, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *eyebrow = lv_label_create(panel);
        app_ui_style_task_intro_label(eyebrow, lv_color_hex(0x0F766E), LV_TEXT_ALIGN_LEFT);
        lv_obj_set_width(eyebrow, 210);
        lv_label_set_text(eyebrow, "\u4E91\u7AEF\u8C03\u5EA6");
        lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 56, 24);

        lv_obj_t *ready_chip = lv_label_create(panel);
        app_ui_style_task_intro_label(ready_chip, lv_color_hex(0x2563EB), LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_width(ready_chip, 180);
        lv_label_set_text(ready_chip, "\u4EFB\u52A1\u5DF2\u5C31\u7EEA");
        lv_obj_align(ready_chip, LV_ALIGN_TOP_RIGHT, -56, 24);

        lv_obj_t *title = lv_label_create(panel);
        app_ui_style_task_intro_label(title, lv_color_hex(0x0F172A), LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_font(title, &font_main_title_cn, 0);
        lv_obj_set_width(title, UI_TASK_INTRO_CONTENT_W);
        lv_label_set_text(title, "\u4E91\u7AEF\u4EFB\u52A1\u5DF2\u63A5\u6536");
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 56, 52);

        lv_obj_t *target_card = lv_obj_create(panel);
        lv_obj_set_size(target_card, UI_TASK_INTRO_CONTENT_W, 78);
        lv_obj_set_style_bg_color(target_card, lv_color_hex(0x073B42), 0);
        lv_obj_set_style_bg_grad_color(target_card, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_bg_grad_dir(target_card, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(target_card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(target_card, 1, 0);
        lv_obj_set_style_border_color(target_card, lv_color_hex(0xA7F3D0), 0);
        lv_obj_set_style_border_opa(target_card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(target_card, 8, 0);
        lv_obj_set_style_pad_all(target_card, 0, 0);
        lv_obj_clear_flag(target_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(target_card, LV_ALIGN_TOP_MID, 0, 96);

        lv_obj_t *target_accent = lv_obj_create(target_card);
        lv_obj_set_size(target_accent, 5, 46);
        lv_obj_set_style_bg_color(target_accent, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_bg_opa(target_accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(target_accent, 0, 0);
        lv_obj_set_style_radius(target_accent, 3, 0);
        lv_obj_set_style_pad_all(target_accent, 0, 0);
        lv_obj_clear_flag(target_accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(target_accent, LV_ALIGN_LEFT_MID, 16, 0);

        lv_obj_t *target_caption = lv_label_create(target_card);
        app_ui_style_task_intro_label(target_caption, lv_color_hex(0xCFFAFE), LV_TEXT_ALIGN_LEFT);
        lv_obj_set_width(target_caption, 150);
        lv_label_set_text(target_caption, "\u76EE\u6807\u6807\u7B7E");
        lv_obj_align(target_caption, LV_ALIGN_LEFT_MID, 34, -11);

        lv_obj_t *target_hint = lv_label_create(target_card);
        app_ui_style_task_intro_label(target_hint, lv_color_hex(0xBAE6FD), LV_TEXT_ALIGN_LEFT);
        lv_obj_set_width(target_hint, 230);
        lv_label_set_text(target_hint, "\u5B9A\u4F4D\u63A5\u5165\u89C6\u89C9");
        lv_obj_align(target_hint, LV_ALIGN_LEFT_MID, 34, 15);

        s_task_intro_target = lv_label_create(target_card);
        app_ui_style_task_intro_label(s_task_intro_target, lv_color_hex(0xFFFFFF), LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_style_text_font(s_task_intro_target, &font_main_title_cn, 0);
        lv_obj_set_width(s_task_intro_target, 220);
        lv_obj_align(s_task_intro_target, LV_ALIGN_RIGHT_MID, -24, 0);

        app_ui_create_task_intro_step(panel,
            "\u901A\u4FE1",
            "\u5DF2\u8FDE\u63A5",
            56,
            194,
            lv_color_hex(0x0F766E),
            lv_color_hex(0xF0FDF4));
        app_ui_create_task_intro_step(panel,
            "\u5929\u6C14",
            "\u6B63\u5E38",
            234,
            194,
            lv_color_hex(0x16A34A),
            lv_color_hex(0xF0FDF4));
        app_ui_create_task_intro_step(panel,
            "\u89C6\u89C9",
            "\u542F\u52A8\u4E2D",
            412,
            194,
            lv_color_hex(0x2563EB),
            lv_color_hex(0xEFF6FF));

        lv_obj_t *footer = lv_label_create(panel);
        app_ui_style_task_intro_label(footer, lv_color_hex(0x64748B), LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(footer, UI_TASK_INTRO_CONTENT_W);
        lv_label_set_text(footer, "\u89C6\u89C9\u542F\u52A8\u4E2D  \u7B49\u5F85\u8FDB\u5165\u753B\u9762");
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -20);
    }
    else
    {
        lv_obj_clear_flag(s_task_intro_layer, LV_OBJ_FLAG_HIDDEN);
    }

    app_ui_set_task_intro_target_unlocked(target_id);
    lv_obj_move_foreground(s_task_intro_layer);
    lv_refr_now(NULL);
    bsp_display_unlock();
    return true;
}

void app_ui_hide_task_intro(void)
{
    if (s_task_intro_layer == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    if (s_task_intro_layer != NULL)
    {
        lv_obj_add_flag(s_task_intro_layer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_refr_now(NULL);
    bsp_display_unlock();
}

void app_ui_set_preview_hud_visible(bool visible)
{
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_hidden(s_hud_layer, !visible);
    app_ui_set_hidden(s_top_bar, !visible);
    app_ui_set_hidden(s_dock, !visible);
    app_ui_set_hidden(s_flow_panel, !visible);
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++)
    {
        app_ui_set_hidden(s_lock_seg[i], !visible);
    }
    if (visible)
    {
        app_ui_move_foreground(s_hud_layer);
        app_ui_move_foreground(s_top_bar);
        app_ui_move_foreground(s_dock);
        app_ui_move_foreground(s_flow_panel);
    }
    bsp_display_unlock();
}

static void app_ui_safety_set_button_enabled_unlocked(bool enabled)
{
    if (s_safety_typhoon_btn == NULL)
    {
        return;
    }
    if (enabled)
    {
        lv_obj_clear_state(s_safety_typhoon_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_safety_typhoon_btn, lv_color_hex(0xB91C1C), 0);
    }
    else
    {
        lv_obj_add_state(s_safety_typhoon_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_safety_typhoon_btn, lv_color_hex(0x64748B), 0);
    }
}

static const char *app_ui_safety_text_or(const char *text, const char *fallback)
{
    return (text != NULL && text[0] != '\0') ? text : fallback;
}

static void app_ui_safety_make_default_view(app_ui_safety_takeover_state_t state,
    int32_t countdown_s,
    uint16_t target_id,
    app_ui_safety_takeover_view_t *out)
{
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = state;
    out->countdown_s = countdown_s;
    out->target_id = target_id;
    out->phase_text = "识别无人机";
    out->status_title = "等待无人机进入画面";
    out->status_detail = "AI推理等待首帧";

    switch (state) {
    case APP_UI_SAFETY_TAKEOVER_STARTING:
        out->phase_text = "摄像头启动";
        out->status_title = "安全接管准备";
        out->status_detail = "打开摄像头，准备识别目标";
        break;
    case APP_UI_SAFETY_TAKEOVER_WAIT_DRONE:
        out->phase_text = "识别无人机";
        out->status_title = "等待无人机进入画面";
        out->status_detail = "AI推理等待首帧";
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_CONFIRMED:
        out->phase_text = "校验TAG身份";
        out->status_title = "无人机已识别";
        out->status_detail = "目标TAG身份校验中";
        break;
    case APP_UI_SAFETY_TAKEOVER_AUTH_PASSED:
        out->phase_text = "身份通过";
        out->status_title = "身份通过";
        out->status_detail = "外门打开，托盘准备伸出";
        break;
    case APP_UI_SAFETY_TAKEOVER_WINDOW_OPEN:
        out->phase_text = "接驳窗口开启";
        out->status_title = "接驳窗口开启";
        out->status_detail = "重新识别无人机，离场后开始倒计时";
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_LOST:
        out->phase_text = "离场倒计时";
        out->status_title = "未检测到无人机";
        out->status_detail = "保持接驳窗口，等待目标返回";
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED:
        out->phase_text = "目标返回";
        out->status_title = "无人机已返回";
        out->status_detail = "继续保持接驳窗口";
        break;
    case APP_UI_SAFETY_TAKEOVER_TYPHOON:
        out->phase_text = "天气保护";
        out->status_title = "安全回收中";
        out->status_detail = "托盘回收，外门关闭";
        break;
    case APP_UI_SAFETY_TAKEOVER_SAFE_DONE:
        out->phase_text = "安全闭环完成";
        out->status_title = "安全闭环完成";
        out->status_detail = "托盘已回收，外门已关闭";
        break;
    case APP_UI_SAFETY_TAKEOVER_FAILED:
        out->phase_text = "保护异常";
        out->status_title = "安全保护异常";
        out->status_detail = "请检查摄像头或从控通信";
        break;
    case APP_UI_SAFETY_TAKEOVER_IDLE:
    default:
        break;
    }
}

static void app_ui_safety_apply_view_unlocked(const app_ui_safety_takeover_view_t *view)
{
    app_ui_safety_takeover_view_t empty = {0};
    app_ui_safety_takeover_view_t fallback = {0};
    if (view == NULL)
    {
        app_ui_safety_make_default_view(APP_UI_SAFETY_TAKEOVER_IDLE, 0, 0, &empty);
        view = &empty;
    }
    app_ui_safety_make_default_view(view->state, view->countdown_s, view->target_id, &fallback);

    char target_buf[48] = {0};
    char count_buf[32] = {0};
    app_ui_safety_format_target(target_buf, sizeof(target_buf), view->target_id);
    app_ui_safety_set_label(s_safety_target, target_buf);

    const char *phase_text = app_ui_safety_text_or(view->phase_text, fallback.phase_text);
    const char *status_title = app_ui_safety_text_or(view->status_title, fallback.status_title);
    const char *status_detail = app_ui_safety_text_or(view->status_detail, fallback.status_detail);
    const char *alert_title = status_title;
    const char *alert_detail = status_detail;

    lv_color_t panel_bg = lv_color_hex(0xFFFFFF);
    lv_color_t banner_border = lv_color_hex(0x7DD3FC);
    lv_color_t bottom_border = lv_color_hex(0xBAE6FD);
    lv_color_t alert_border = lv_color_hex(0xBAE6FD);
    lv_color_t accent = lv_color_hex(0x0EA5E9);
    lv_color_t phase_color = lv_color_hex(0x0369A1);
    lv_color_t title_text = lv_color_hex(0x0F172A);
    lv_color_t detail_text = lv_color_hex(0x475569);
    lv_color_t layer_bg = lv_color_hex(0x000000);
    lv_opa_t layer_bg_opa = LV_OPA_TRANSP;
    bool show_alert = false;
    bool show_count = false;
    bool typhoon_enabled = true;

    switch (view->state) {
    case APP_UI_SAFETY_TAKEOVER_STARTING:
    case APP_UI_SAFETY_TAKEOVER_WAIT_DRONE:
    case APP_UI_SAFETY_TAKEOVER_DRONE_CONFIRMED:
    case APP_UI_SAFETY_TAKEOVER_WINDOW_OPEN:
        break;
    case APP_UI_SAFETY_TAKEOVER_AUTH_PASSED:
        accent = lv_color_hex(0x0F766E);
        phase_color = lv_color_hex(0x0F766E);
        banner_border = lv_color_hex(0x99F6E4);
        bottom_border = lv_color_hex(0x99F6E4);
        alert_border = lv_color_hex(0x99F6E4);
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_LOST:
        accent = lv_color_hex(0xD97706);
        phase_color = lv_color_hex(0xB45309);
        banner_border = lv_color_hex(0xFBBF24);
        bottom_border = lv_color_hex(0xFDE68A);
        alert_border = lv_color_hex(0xF59E0B);
        show_alert = true;
        show_count = true;
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED:
        accent = lv_color_hex(0x0F766E);
        phase_color = lv_color_hex(0x0F766E);
        banner_border = lv_color_hex(0x99F6E4);
        bottom_border = lv_color_hex(0x99F6E4);
        alert_border = lv_color_hex(0x5EEAD4);
        show_alert = true;
        break;
    case APP_UI_SAFETY_TAKEOVER_TYPHOON:
        accent = lv_color_hex(0xDC2626);
        phase_color = lv_color_hex(0xB91C1C);
        banner_border = lv_color_hex(0xFCA5A5);
        bottom_border = lv_color_hex(0xFCA5A5);
        alert_border = lv_color_hex(0xF87171);
        layer_bg = lv_color_hex(0x7F1D1D);
        layer_bg_opa = (lv_opa_t)46;
        show_alert = true;
        typhoon_enabled = false;
        break;
    case APP_UI_SAFETY_TAKEOVER_SAFE_DONE:
        accent = lv_color_hex(0x16A34A);
        phase_color = lv_color_hex(0x15803D);
        banner_border = lv_color_hex(0x86EFAC);
        bottom_border = lv_color_hex(0xBBF7D0);
        alert_border = lv_color_hex(0x86EFAC);
        show_alert = true;
        typhoon_enabled = false;
        break;
    case APP_UI_SAFETY_TAKEOVER_FAILED:
        accent = lv_color_hex(0xDC2626);
        phase_color = lv_color_hex(0xB91C1C);
        banner_border = lv_color_hex(0xFCA5A5);
        bottom_border = lv_color_hex(0xFCA5A5);
        alert_border = lv_color_hex(0xF87171);
        show_alert = true;
        typhoon_enabled = false;
        break;
    case APP_UI_SAFETY_TAKEOVER_IDLE:
    default:
        break;
    }

    lv_obj_set_style_bg_color(s_safety_layer, layer_bg, 0);
    lv_obj_set_style_bg_opa(s_safety_layer, layer_bg_opa, 0);
    lv_obj_set_style_bg_color(s_safety_banner, panel_bg, 0);
    lv_obj_set_style_border_color(s_safety_banner, banner_border, 0);
    lv_obj_set_style_bg_color(s_safety_status, panel_bg, 0);
    lv_obj_set_style_border_color(s_safety_status, bottom_border, 0);
    lv_obj_set_style_bg_color(s_safety_alert, panel_bg, 0);
    lv_obj_set_style_border_color(s_safety_alert, alert_border, 0);
    lv_obj_set_style_bg_color(s_safety_top_line, accent, 0);
    lv_obj_set_style_bg_color(s_safety_status_dot, accent, 0);
    lv_obj_set_style_text_color(s_safety_title, title_text, 0);
    lv_obj_set_style_text_color(s_safety_phase, phase_color, 0);
    lv_obj_set_style_text_color(s_safety_target, title_text, 0);
    lv_obj_set_style_text_color(s_safety_alert_title, title_text, 0);
    lv_obj_set_style_text_color(s_safety_alert_detail, detail_text, 0);
    lv_obj_set_style_text_color(s_safety_count, accent, 0);
    lv_obj_set_style_text_color(s_safety_status_title, title_text, 0);
    lv_obj_set_style_text_color(s_safety_status_detail, detail_text, 0);

    app_ui_safety_set_label(s_safety_phase, phase_text);
    app_ui_safety_set_label(s_safety_alert_title, alert_title);
    app_ui_safety_set_label(s_safety_alert_detail, alert_detail);
    app_ui_safety_set_label(s_safety_status_title, status_title);
    app_ui_safety_set_label(s_safety_status_detail, status_detail);
    app_ui_safety_apply_alert_visibility_unlocked(show_alert, view->state);
    if (show_count)
    {
        snprintf(count_buf,
            sizeof(count_buf),
            "剩余 %lds",
            (long)((view->countdown_s > 0) ? view->countdown_s : 1));
        app_ui_safety_set_label(s_safety_count, count_buf);
        app_ui_safety_set_hidden(s_safety_count, false);
    }
    else
    {
        app_ui_safety_set_label(s_safety_count, "");
        app_ui_safety_set_hidden(s_safety_count, true);
    }
    app_ui_safety_set_button_enabled_unlocked(typhoon_enabled);
}

void app_ui_set_safety_typhoon_callback(app_ui_safety_typhoon_cb_t cb)
{
    s_safety_typhoon_cb = cb;
}

void app_ui_safety_takeover_set_visible(bool visible)
{
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    lv_obj_t *scr = app_ui_get_active_screen();
    app_ui_create_safety_takeover_unlocked(scr);
    if (!visible)
    {
        app_ui_safety_alert_timer_stop_unlocked();
        s_safety_alert_state = APP_UI_SAFETY_TAKEOVER_IDLE;
        s_safety_alert_flash_done = false;
        app_ui_safety_set_hidden(s_safety_alert, true);
    }
    app_ui_safety_set_hidden(s_safety_layer, !visible);
    if (visible)
    {
        app_ui_move_foreground(s_safety_layer);
    }
    bsp_display_unlock();
}

bool app_ui_safety_takeover_set_view(const app_ui_safety_takeover_view_t *view)
{
    if (view == NULL)
    {
        return false;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return false;
    }
    lv_obj_t *scr = app_ui_get_active_screen();
    app_ui_create_safety_takeover_unlocked(scr);
    app_ui_safety_apply_view_unlocked(view);
    app_ui_safety_set_hidden(s_safety_layer, false);
    app_ui_move_foreground(s_safety_layer);
    bsp_display_unlock();
    return true;
}

void app_ui_safety_takeover_set_state(app_ui_safety_takeover_state_t state,
    int32_t countdown_s,
    uint16_t target_id)
{
    app_ui_safety_takeover_view_t view = {0};
    app_ui_safety_make_default_view(state, countdown_s, target_id, &view);
    (void)app_ui_safety_takeover_set_view(&view);
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
    app_ui_set_top_chip_text_unlocked(s_status, "任务", status_display);
    app_ui_set_top_chip_text_unlocked(s_hint, "标签", "等待识别");
    app_ui_flow_snapshot_t flow = {0};
    app_ui_flow_snapshot_default(status_display, &flow);
    app_ui_update_flow_unlocked(&flow);
    s_control_cache_valid = false;
    bsp_display_unlock();
}
// 更新视觉框、准星颜色、锁定条和认证提示；调用者需持有 LVGL 锁。
static void app_ui_update_hud_unlocked(const char *dock_text,
    const app_vision_result_t *vision,
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
    app_ui_update_telemetry_unlocked(dock_text, dock);
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
    if (!app_ui_control_needs_update(status_display, vision_display, dock_text, vision, dock, flow))
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_set_header_unlocked(status_display, vision_display, dock_text, dock);
    app_ui_update_flow_unlocked(flow);
    app_ui_update_hud_unlocked(dock_text, vision, dock);
    app_ui_store_control_cache(status_display, vision_display, dock_text, vision, dock, flow);
    bsp_display_unlock();
}
