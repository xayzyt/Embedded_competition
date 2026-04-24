#include "app_ui.h"
#include <stdio.h>
#include <inttypes.h>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#ifndef BSP_CAMERA_ROTATION
#define BSP_CAMERA_ROTATION 0
#endif
#define HUD_SRC_W               240
#define HUD_SRC_H               180
#define HUD_LOCK_SEG_COUNT      5
#define HUD_AUTH_SHOW_MS        1200
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
static bool s_have_last_box = false;
static int32_t s_last_box_x = 0;
static int32_t s_last_box_y = 0;
static int32_t s_last_box_w = 0;
static int32_t s_last_box_h = 0;
static app_dock_state_t s_last_hud_state = APP_DOCK_STATE_SEARCHING;
static uint32_t s_auth_deadline_ms = 0;
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
static void app_ui_style_label(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202020), 0);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    lv_obj_set_style_radius(obj, 4, 0);
}
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
static void app_ui_style_cross_line(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x24D1A0), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}
static void app_ui_style_track_box(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 3, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFD34D), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static void app_ui_style_lock_seg(lv_obj_t *obj)
{
    lv_obj_set_style_radius(obj, 3, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x808080), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, 0);
}
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
static void app_ui_calc_fit_dims(int32_t src_w, int32_t src_h, int32_t *fit_w, int32_t *fit_h)
{
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
static void app_ui_get_rotated_dims(int32_t *rot_w, int32_t *rot_h)
{
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
static void app_ui_transform_src_point(float x, float y, float *out_x, float *out_y)
{
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
static int32_t app_ui_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static void app_ui_map_bbox_to_screen(const app_vision_result_t *vision,
                                      int32_t *x,
                                      int32_t *y,
                                      int32_t *w,
                                      int32_t *h)
{
    if ((vision == NULL) || (x == NULL) || (y == NULL) || (w == NULL) || (h == NULL)) {
        return;
    }
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
    int32_t rot_w = HUD_SRC_W;
    int32_t rot_h = HUD_SRC_H;
    app_ui_get_rotated_dims(&rot_w, &rot_h);
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
        app_ui_transform_src_point(pts[i][0], pts[i][1], &rx, &ry);
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
    if ((*x + *w) > BSP_LCD_H_RES) {
        *w = BSP_LCD_H_RES - *x;
    }
    if ((*y + *h) > BSP_LCD_V_RES) {
        *h = BSP_LCD_V_RES - *y;
    }
}
static void app_ui_update_lock_bar(const app_dock_judge_result_t *dock)
{
    uint8_t filled = 0;
    lv_color_t active = lv_color_hex(0xFFD34D);
    if (dock != NULL) {
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
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {
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
static void app_ui_update_auth_banner(app_dock_state_t state)
{
    uint32_t now_ms = lv_tick_get();
    if ((state == APP_DOCK_STATE_READY_TO_DOCK) &&
        (s_last_hud_state != APP_DOCK_STATE_READY_TO_DOCK)) {
        s_auth_deadline_ms = now_ms + HUD_AUTH_SHOW_MS;
    }
    if ((s_auth != NULL) && (s_auth_deadline_ms != 0U) && (now_ms <= s_auth_deadline_ms)) {
        lv_obj_clear_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    } else if (s_auth != NULL) {
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }
    s_last_hud_state = state;
}
static void app_ui_set_track_box(bool show,
                                 int32_t x,
                                 int32_t y,
                                 int32_t w,
                                 int32_t h,
                                 lv_color_t color,
                                 lv_opa_t opa)
{
    if (s_track_box == NULL) {
        return;
    }
    if (!show) {
        lv_obj_add_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(s_track_box, x, y);
    lv_obj_set_size(s_track_box, w, h);
    lv_obj_set_style_border_color(s_track_box, color, 0);
    lv_obj_set_style_border_opa(s_track_box, opa, 0);
    lv_obj_clear_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
}
bool app_ui_create(void)
{
    if (!bsp_display_lock(0)) {
        return false;
    }
    lv_obj_t *scr = app_get_active_screen();
    if (s_hud_layer == NULL) {
        s_hud_layer = lv_obj_create(scr);
        app_ui_style_hud_layer(s_hud_layer);
        lv_obj_set_size(s_hud_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_align(s_hud_layer, LV_ALIGN_CENTER, 0, 0);
    }
    if (s_track_box == NULL) {
        s_track_box = lv_obj_create(s_hud_layer);
        app_ui_style_track_box(s_track_box);
    }
    if (s_cross_h == NULL) {
        s_cross_h = lv_obj_create(s_hud_layer);
        app_ui_style_cross_line(s_cross_h);
        lv_obj_set_size(s_cross_h, 48, 2);
        lv_obj_align(s_cross_h, LV_ALIGN_CENTER, 0, 0);
    }
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
        if (s_lock_seg[i] == NULL) {
            s_lock_seg[i] = lv_obj_create(scr);
            app_ui_style_lock_seg(s_lock_seg[i]);
            lv_obj_set_size(s_lock_seg[i], seg_w, seg_h);
            lv_obj_set_pos(s_lock_seg[i], start_x + i * (seg_w + seg_gap), y);
        }
    }
    if (s_auth == NULL) {
        s_auth = lv_label_create(scr);
        app_ui_style_auth_label(s_auth);
        lv_label_set_text(s_auth, "AUTH PASSED");
        lv_obj_align(s_auth, LV_ALIGN_CENTER, 0, -32);
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_status == NULL) {
        s_status = lv_label_create(scr);
        app_ui_style_label(s_status);
        lv_label_set_text(s_status, "dock: init");
        lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 8, 8);
    }
    if (s_vision == NULL) {
        s_vision = lv_label_create(scr);
        app_ui_style_label(s_vision);
        lv_label_set_text(s_vision, "vision: init");
        lv_obj_align(s_vision, LV_ALIGN_TOP_RIGHT, -8, 8);
    }
    if (s_dock == NULL) {
        s_dock = lv_label_create(scr);
        app_ui_style_label(s_dock);
        lv_label_set_text(s_dock, "dock dbg: init");
        lv_obj_set_width(s_dock, BSP_LCD_H_RES - 220);
        lv_obj_set_style_text_align(s_dock, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    if (s_hint == NULL) {
        s_hint = lv_label_create(scr);
        app_ui_style_hint_label(s_hint);
        lv_label_set_text(s_hint, "cloud dispatch enabled / no touch control");
        lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 46);
    }
    if (s_status) lv_obj_move_foreground(s_status);
    if (s_vision) lv_obj_move_foreground(s_vision);
    if (s_coord) lv_obj_move_foreground(s_coord);
    if (s_dock) lv_obj_move_foreground(s_dock);
    if (s_hint) lv_obj_move_foreground(s_hint);
    if (s_auth) lv_obj_move_foreground(s_auth);
    bsp_display_unlock();
    return true;
}
void app_ui_set_status(const char *text)
{
    if ((text == NULL) || (s_status == NULL)) {
        return;
    }
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_status, text);
    bsp_display_unlock();
}
void app_ui_set_coord(int32_t x, int32_t y)
{
    (void)x;
    (void)y;
}
void app_ui_set_vision_text(const char *text)
{
    if ((text == NULL) || (s_vision == NULL)) {
        return;
    }
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_vision, text);
    bsp_display_unlock();
}
void app_ui_set_dock_text(const char *text)
{
    if ((text == NULL) || (s_dock == NULL)) {
        return;
    }
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_dock, text);
    bsp_display_unlock();
}
void app_ui_set_hint_text(const char *text)
{
    if ((text == NULL) || (s_hint == NULL)) {
        return;
    }
    if (!bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_hint, text);
    bsp_display_unlock();
}
void app_ui_update_hud(const app_vision_result_t *vision,
                       const app_dock_judge_result_t *dock)
{
    if ((dock == NULL) || (s_hud_layer == NULL) || (s_track_box == NULL)) {
        return;
    }
    if (!bsp_display_lock(0)) {
        return;
    }
    bool hold_box = false;
    bool show_box = false;
    int32_t box_x = 0;
    int32_t box_y = 0;
    int32_t box_w = 0;
    int32_t box_h = 0;
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
        box_x = s_last_box_x;
        box_y = s_last_box_y;
        box_w = s_last_box_w;
        box_h = s_last_box_h;
        show_box = true;
        hold_box = true;
    } else {
        s_have_last_box = false;
    }
    lv_color_t box_color = app_ui_state_color(dock->state, hold_box);
    lv_opa_t box_opa = hold_box ? LV_OPA_50 : LV_OPA_COVER;
    app_ui_set_track_box(show_box, box_x, box_y, box_w, box_h, box_color, box_opa);
    if ((s_cross_h != NULL) && (s_cross_v != NULL)) {
        lv_color_t cross_color = app_ui_state_color(dock->state, false);
        lv_obj_set_style_bg_color(s_cross_h, cross_color, 0);
        lv_obj_set_style_bg_color(s_cross_v, cross_color, 0);
    }
    app_ui_update_lock_bar(dock);
    app_ui_update_auth_banner(dock->state);
    bsp_display_unlock();
}
