#include "app_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// 主屏 UI：展示任务阶段、连接状态、天气、时钟、异常演示和语音开关。

const lv_image_dsc_t *app_ui_weather_image_src(int weather_code);

LV_FONT_DECLARE(font_loading_cn)
LV_FONT_DECLARE(font_main_title_cn)
LV_FONT_DECLARE(font_button_cn)
LV_FONT_DECLARE(font_title_en)
LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk)
LV_IMAGE_DECLARE(logo);
#define UI_LOCK_SHORT_MS        30
#define UI_LOCK_BOOT_MS         300
#define UI_PROJECT_NAME         "基于ESP32-P4双芯协同的低空接驳微港与端侧鉴权闭环系统"
#define UI_CONTEST_NAME         "第九届（2026）全国大学生嵌入式芯片与系统设计竞赛"
#define UI_TEAM_NAME            "地线引力队"
#define UI_CLOCK_VALID_EPOCH    1704067200LL
#define UI_CLOCK_TIMEZONE       "CST-8"
#define UI_TASK_BLINK_MS        520
#define UI_MAIN_PHASE_COUNT     3
#define UI_MAIN_PHASE_DOCK_IDX  2
#define UI_EXCEPTION_PHASE_COUNT 4
static lv_obj_t *s_main_layer = NULL;
static lv_obj_t *s_main_wifi_ind = NULL;
static lv_obj_t *s_main_mqtt_ind = NULL;
static lv_obj_t *s_main_ch32_ind = NULL;
static lv_obj_t *s_main_task_accent = NULL;
static lv_obj_t *s_main_task_badge = NULL;
static lv_obj_t *s_main_task_label = NULL;
static lv_obj_t *s_main_task_card_label = NULL;
static lv_obj_t *s_main_task_hint_label = NULL;
static lv_obj_t *s_main_task_alert = NULL;
static lv_obj_t *s_main_task_alert_label = NULL;
static lv_obj_t *s_main_phase_box[UI_MAIN_PHASE_COUNT] = {0};
static lv_obj_t *s_main_phase_label[UI_MAIN_PHASE_COUNT] = {0};
static lv_obj_t *s_main_conn_label = NULL;
static lv_obj_t *s_main_dock_label = NULL;
static lv_obj_t *s_main_clock_time = NULL;
static lv_obj_t *s_main_clock_note = NULL;
static lv_obj_t *s_main_weather_title = NULL;
static lv_obj_t *s_main_weather_icon = NULL;
static lv_obj_t *s_main_weather_label = NULL;
static lv_obj_t *s_main_exception_btn = NULL;
static lv_obj_t *s_main_exception_label = NULL;
static lv_obj_t *s_main_voice_btn = NULL;
static lv_obj_t *s_main_voice_label = NULL;
static lv_obj_t *s_exception_layer = NULL;
static lv_obj_t *s_exception_status_label = NULL;
static lv_obj_t *s_exception_detail_label = NULL;
static lv_obj_t *s_exception_ch32_label = NULL;
static lv_obj_t *s_exception_weather_title = NULL;
static lv_obj_t *s_exception_weather_icon = NULL;
static lv_obj_t *s_exception_weather_value_label = NULL;
static lv_obj_t *s_exception_weather_btn = NULL;
static lv_obj_t *s_exception_weather_btn_label = NULL;
static lv_obj_t *s_exception_back_btn = NULL;
static lv_obj_t *s_exception_back_label = NULL;
static lv_obj_t *s_exception_phase_box[UI_EXCEPTION_PHASE_COUNT] = {0};
static lv_obj_t *s_exception_phase_label[UI_EXCEPTION_PHASE_COUNT] = {0};
static lv_timer_t *s_main_clock_timer = NULL;
static lv_timer_t *s_main_task_blink_timer = NULL;
static app_ui_exception_demo_cb_t s_exception_demo_cb = NULL;
static app_ui_voice_toggle_cb_t s_voice_toggle_cb = NULL;
static app_ui_exception_back_cb_t s_exception_back_cb = NULL;
static app_ui_weather_sim_cb_t s_weather_sim_cb = NULL;
static bool s_clock_tz_set = false;
static bool s_main_weather_simulated = false;
static bool s_main_task_blink_enabled = false;
static bool s_main_task_blink_dim = false;
static bool s_main_status_seen = false;
static bool s_main_status_wifi_ok = false;
static bool s_main_status_mqtt_ok = false;
static bool s_main_status_ch32_ok = false;
static bool s_main_task_weather_blocked = false;
static bool s_main_voice_enabled = true;
static app_ui_main_task_state_t s_main_task_state = APP_UI_MAIN_TASK_WAITING;
static app_ui_exception_demo_state_t s_exception_state = APP_UI_EXCEPTION_DEMO_READY;
static int s_exception_last_stage = 0;
static uint8_t s_exception_last_error = 0;
static char s_main_weather_text[96] = "同步中";
static int s_main_weather_code = 99;
static char s_exception_weather_text[96] = "晴\n31℃";
static int s_exception_weather_code = 0;
static lv_obj_t *app_get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}
static lv_obj_t *app_ui_button_create(lv_obj_t *parent)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_button_create(parent);
#else
    return lv_btn_create(parent);
#endif
}
// 主屏卡片面板统一样式。
static lv_obj_t *app_ui_create_soft_panel(lv_obj_t *parent,
    int32_t w,
    int32_t h,
    int32_t radius)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 8, 0);
    lv_obj_set_style_shadow_offset_y(panel, 2, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_shadow_opa(panel, (lv_opa_t)48, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}
void app_ui_set_exception_demo_callback(app_ui_exception_demo_cb_t cb)
{
    s_exception_demo_cb = cb;
}
void app_ui_set_voice_toggle_callback(app_ui_voice_toggle_cb_t cb)
{
    s_voice_toggle_cb = cb;
}
void app_ui_set_exception_back_callback(app_ui_exception_back_cb_t cb)
{
    s_exception_back_cb = cb;
}
void app_ui_set_weather_sim_callback(app_ui_weather_sim_cb_t cb)
{
    s_weather_sim_cb = cb;
}

/* ---------- 主屏按钮事件 ---------- */

static void app_ui_exception_demo_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (s_exception_demo_cb != NULL)
    {
        s_exception_demo_cb();
    }
}
static void app_ui_voice_toggle_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (s_voice_toggle_cb != NULL)
    {
        s_voice_toggle_cb();
    }
}
// 天气模拟按钮事件，用于演示恶劣天气保护。
static void app_ui_weather_sim_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (s_weather_sim_cb != NULL)
    {
        s_weather_sim_cb();
    }
}
static void app_ui_exception_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (s_exception_back_cb != NULL)
    {
        s_exception_back_cb();
    }
    else
    {
        (void)app_ui_show_main_screen();
    }
}
// 创建 Wi-Fi/MQTT/CH32 三个圆点状态灯。
static lv_obj_t *app_ui_create_status_dot(lv_obj_t *parent,
    const char *label_text,
    int32_t x,
    int32_t y)
{
    lv_obj_t *ind = lv_obj_create(parent);
    lv_obj_set_size(ind, 10, 10);
    lv_obj_set_style_radius(ind, 5, 0);
    lv_obj_set_style_border_width(ind, 1, 0);
    lv_obj_set_style_border_color(ind, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(ind, lv_color_hex(0xD85B5B), 0);
    lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_align(ind, LV_ALIGN_TOP_LEFT, x, y + 6);
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(lbl, &font_loading_cn, 0);
    lv_label_set_text(lbl, label_text);
    lv_obj_align_to(lbl, ind, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    return ind;
}
// 更新时间显示；SNTP 未同步前显示等待状态。
static void app_ui_update_clock_unlocked(void)
{
    if (s_main_clock_time == NULL || s_main_clock_note == NULL)
    {
        return;
    }
    if (!s_clock_tz_set)
    {
        setenv("TZ", UI_CLOCK_TIMEZONE, 1);
        tzset();
        s_clock_tz_set = true;
    }
    time_t now = 0;
    time(&now);
    if ((int64_t)now < UI_CLOCK_VALID_EPOCH)
    {
        lv_label_set_text(s_main_clock_time, "--:--:--");
        lv_label_set_text(s_main_clock_note, "WAIT SYNC");
        lv_obj_set_style_text_color(s_main_clock_time, lv_color_hex(0x94A3B8), 0);
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
    lv_obj_set_style_text_color(s_main_clock_time, lv_color_hex(0x0F172A), 0);
}
/* ---------- 时钟与任务视觉状态 ---------- */

static void app_ui_clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    app_ui_update_clock_unlocked();
}
static void app_ui_set_main_task_opa_unlocked(lv_opa_t opa)
{
    if (s_main_task_label != NULL)
    {
        lv_obj_set_style_text_opa(s_main_task_label, opa, 0);
    }
    if (s_main_task_card_label != NULL)
    {
        lv_obj_set_style_text_opa(s_main_task_card_label, opa, 0);
    }
    if (s_main_task_alert_label != NULL)
    {
        lv_obj_set_style_text_opa(s_main_task_alert_label, opa, 0);
    }
}
static void app_ui_main_task_blink_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_main_task_blink_enabled)
    {
        app_ui_set_main_task_opa_unlocked(LV_OPA_COVER);
        return;
    }
    s_main_task_blink_dim = !s_main_task_blink_dim;
    app_ui_set_main_task_opa_unlocked(s_main_task_blink_dim ? LV_OPA_70 : LV_OPA_COVER);
}
// 天气阻止等告警状态通过轻微闪烁提醒用户。
static void app_ui_set_main_task_blink_unlocked(bool enabled)
{
    s_main_task_blink_enabled = enabled;
    s_main_task_blink_dim = false;
    app_ui_set_main_task_opa_unlocked(LV_OPA_COVER);
    if (s_main_task_blink_timer == NULL)
    {
        return;
    }
    if (enabled)
    {
        lv_timer_reset(s_main_task_blink_timer);
        lv_timer_resume(s_main_task_blink_timer);
    }
    else
    {
        lv_timer_pause(s_main_task_blink_timer);
    }
}
// 更新阶段指示块颜色：ready 绿色、异常红色、未完成灰色。
static void app_ui_apply_main_phase_style_unlocked(int index, bool ready, bool danger)
{
    if (index < 0 || index >= UI_MAIN_PHASE_COUNT)
    {
        return;
    }
    if (s_main_phase_box[index] == NULL || s_main_phase_label[index] == NULL)
    {
        return;
    }
    lv_obj_set_style_bg_color(s_main_phase_box[index],
        danger ? lv_color_hex(0xFEF2F2) :
            (ready ? lv_color_hex(0xECFDF5) : lv_color_hex(0xF8FAFC)),
        0);
    lv_obj_set_style_border_color(s_main_phase_box[index],
        danger ? lv_color_hex(0xFCA5A5) :
            (ready ? lv_color_hex(0x5EEAD4) : lv_color_hex(0xE2E8F0)),
        0);
    lv_obj_set_style_text_color(s_main_phase_label[index],
        danger ? lv_color_hex(0x991B1B) :
            (ready ? lv_color_hex(0x0F766E) : lv_color_hex(0x475569)),
        0);
}
// 根据连接和天气状态刷新顶部任务徽标。
static void app_ui_update_main_task_badge_unlocked(void)
{
    if (s_main_task_badge == NULL)
    {
        return;
    }
    const bool cloud_ready = s_main_status_wifi_ok && s_main_status_mqtt_ok;
    const lv_color_t normal = lv_color_hex(0x0F766E);
    const lv_color_t danger = lv_color_hex(0xB91C1C);
    const lv_color_t muted = lv_color_hex(0x64748B);
    const char *text = "状态同步中";
    lv_color_t color = muted;
    if (s_main_task_weather_blocked)
    {
        text = "恶劣天气 禁止接驳";
        color = danger;
    }
    else if (!s_main_status_seen)
    {
        text = "状态同步中";
        color = muted;
    }
    else if (!cloud_ready)
    {
        text = "云端通信未就绪";
        color = danger;
    }
    else if (!s_main_status_ch32_ok)
    {
        text = "CH32通信未就绪";
        color = danger;
    }
    else
    {
        text = "接驳已就绪";
        color = normal;
    }
    lv_label_set_text(s_main_task_badge, text);
    lv_obj_set_style_text_color(s_main_task_badge, color, 0);
}
// 同步三个阶段指示：云端、从控、对接。
static void app_ui_refresh_main_phase_indicators_unlocked(void)
{
    const bool cloud_ready = s_main_status_wifi_ok && s_main_status_mqtt_ok;
    const bool link_ready = cloud_ready && s_main_status_ch32_ok;
    app_ui_apply_main_phase_style_unlocked(0,
        s_main_status_seen && cloud_ready,
        s_main_status_seen && !cloud_ready);
    app_ui_apply_main_phase_style_unlocked(1,
        s_main_status_seen && link_ready,
        s_main_status_seen && !link_ready);
    app_ui_apply_main_phase_style_unlocked(UI_MAIN_PHASE_DOCK_IDX,
        s_main_task_state == APP_UI_MAIN_TASK_COMPLETED,
        s_main_task_weather_blocked);
    app_ui_update_main_task_badge_unlocked();
}
// 天气阻止时切换任务卡片强调色并显示告警块。
static void app_ui_apply_main_task_visual_unlocked(bool weather_blocked)
{
    const lv_color_t normal = lv_color_hex(0x0F766E);
    const lv_color_t danger = lv_color_hex(0xB91C1C);
    s_main_task_weather_blocked = weather_blocked;
    if (s_main_task_accent != NULL)
    {
        lv_obj_set_style_bg_color(s_main_task_accent, weather_blocked ? danger : normal, 0);
    }
    if (s_main_task_hint_label != NULL)
    {
        if (weather_blocked)
        {
            lv_obj_add_flag(s_main_task_hint_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(s_main_task_hint_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_main_task_alert != NULL)
    {
        if (weather_blocked)
        {
            lv_obj_clear_flag(s_main_task_alert, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_main_task_alert, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_main_task_alert_label != NULL)
    {
        lv_label_set_text(s_main_task_alert_label, "禁止接驳");
    }
    app_ui_refresh_main_phase_indicators_unlocked();
}
static bool app_ui_weather_is_severe(int weather_code)
{
    return weather_code == 36 || weather_code == 37 || weather_code == 38;
}
static bool app_ui_weather_uses_original_icon(int weather_code)
{
    return weather_code >= 0 && weather_code <= 3;
}
/* ---------- 天气策略展示 ---------- */

static void app_ui_update_main_exception_button_unlocked(void)
{
    if (s_main_exception_btn != NULL)
    {
        lv_obj_set_style_bg_color(s_main_exception_btn, lv_color_hex(0x0F766E), 0);
    }
    if (s_main_exception_label != NULL)
    {
        lv_obj_set_style_text_font(s_main_exception_label, &font_button_cn, 0);
        lv_label_set_text(s_main_exception_label, "安全接管");
        lv_obj_center(s_main_exception_label);
    }
    if (s_main_exception_btn != NULL)
    {
        lv_obj_invalidate(s_main_exception_btn);
    }
}
static void app_ui_update_main_voice_button_unlocked(void)
{
    if (s_main_voice_label != NULL)
    {
        lv_obj_set_style_text_font(s_main_voice_label, &font_button_cn, 0);
        lv_label_set_text(s_main_voice_label, s_main_voice_enabled ? "语音打开" : "语音关闭");
        lv_obj_center(s_main_voice_label);
    }
    if (s_main_voice_btn != NULL)
    {
        lv_obj_set_style_bg_color(s_main_voice_btn,
            s_main_voice_enabled ? lv_color_hex(0x0F766E) : lv_color_hex(0x64748B),
            0);
        lv_obj_invalidate(s_main_voice_btn);
    }
}
// 刷新天气图标、天气文案和模拟按钮状态。
static void app_ui_apply_weather_unlocked(void)
{
    const bool extreme = app_ui_weather_is_severe(s_main_weather_code);
    const bool use_original_icon = app_ui_weather_uses_original_icon(s_main_weather_code);
    const lv_color_t accent = extreme ? lv_color_hex(0xDC2626) : lv_color_hex(0x0F766E);
    if (s_main_weather_title != NULL)
    {
        lv_label_set_text(s_main_weather_title, "云端天气");
        lv_obj_set_style_text_color(s_main_weather_title, lv_color_hex(0x64748B), 0);
    }
    if (s_main_weather_icon != NULL)
    {
        lv_image_set_src(s_main_weather_icon, app_ui_weather_image_src(s_main_weather_code));
        lv_obj_set_style_image_recolor(s_main_weather_icon, accent, 0);
        lv_obj_set_style_image_recolor_opa(s_main_weather_icon,
            (!extreme && use_original_icon) ? LV_OPA_TRANSP : LV_OPA_COVER,
            0);
    }
    if (s_main_weather_label != NULL)
    {
        lv_label_set_text(s_main_weather_label, s_main_weather_text);
        lv_obj_set_style_text_color(s_main_weather_label,
            extreme ? lv_color_hex(0x991B1B) : lv_color_hex(0x15803D),
            0);
    }
    app_ui_update_main_exception_button_unlocked();
}

static void app_ui_exception_apply_weather_unlocked(void)
{
    const bool extreme = app_ui_weather_is_severe(s_exception_weather_code);
    const bool use_original_icon = app_ui_weather_uses_original_icon(s_exception_weather_code);
    const lv_color_t accent = extreme ? lv_color_hex(0xDC2626) : lv_color_hex(0x0F766E);
    if (s_exception_weather_title != NULL)
    {
        lv_label_set_text(s_exception_weather_title, "演示天气");
        lv_obj_set_style_text_color(s_exception_weather_title,
            extreme ? lv_color_hex(0xB91C1C) : lv_color_hex(0x64748B),
            0);
    }
    if (s_exception_weather_icon != NULL)
    {
        lv_image_set_src(s_exception_weather_icon, app_ui_weather_image_src(s_exception_weather_code));
        lv_obj_set_style_image_recolor(s_exception_weather_icon, accent, 0);
        lv_obj_set_style_image_recolor_opa(s_exception_weather_icon,
            (!extreme && use_original_icon) ? LV_OPA_TRANSP : LV_OPA_COVER,
            0);
    }
    if (s_exception_weather_value_label != NULL)
    {
        lv_label_set_text(s_exception_weather_value_label, s_exception_weather_text);
        lv_obj_set_style_text_color(s_exception_weather_value_label,
            extreme ? lv_color_hex(0x991B1B) : lv_color_hex(0x15803D),
            0);
    }
}

static void app_ui_exception_set_weather_unlocked(bool severe)
{
    if (severe)
    {
        strlcpy(s_exception_weather_text, "台风\n28℃", sizeof(s_exception_weather_text));
        s_exception_weather_code = 36;
    }
    else
    {
        strlcpy(s_exception_weather_text, s_main_weather_text, sizeof(s_exception_weather_text));
        s_exception_weather_code = s_main_weather_code;
    }
    app_ui_exception_apply_weather_unlocked();
}

static void app_ui_exception_set_weather_button_unlocked(const char *text, bool enabled)
{
    if (s_exception_weather_btn_label != NULL)
    {
        lv_label_set_text(s_exception_weather_btn_label, text);
        lv_obj_center(s_exception_weather_btn_label);
    }
    if (s_exception_weather_btn != NULL)
    {
        if (enabled)
        {
            lv_obj_clear_state(s_exception_weather_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_exception_weather_btn, lv_color_hex(0xB91C1C), 0);
        }
        else
        {
            lv_obj_add_state(s_exception_weather_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_exception_weather_btn, lv_color_hex(0x94A3B8), 0);
        }
    }
}

static void app_ui_exception_set_back_button_unlocked(const char *text, bool visible)
{
    if (s_exception_back_label != NULL)
    {
        lv_label_set_text(s_exception_back_label, text);
        lv_obj_center(s_exception_back_label);
    }
    if (s_exception_back_btn != NULL)
    {
        if (visible)
        {
            lv_obj_clear_flag(s_exception_back_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_exception_back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
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
static void app_ui_apply_main_task_display_unlocked(const char *text,
    lv_color_t color,
    bool blink,
    bool weather_blocked)
{
    if (text == NULL)
    {
        return;
    }
    if (s_main_weather_simulated)
    {
        app_ui_apply_main_task_visual_unlocked(true);
        app_ui_set_main_task_blink_unlocked(true);
        app_ui_set_main_task_text_unlocked("恶劣天气", lv_color_hex(0xB91C1C));
        return;
    }
    app_ui_apply_main_task_visual_unlocked(weather_blocked);
    app_ui_set_main_task_blink_unlocked(blink);
    app_ui_set_main_task_text_unlocked(text, color);
}
// 兼容旧的英文状态字符串，把它们归一到主屏枚举。
static app_ui_main_task_state_t app_ui_main_task_state_from_text(const char *text)
{
    if (text == NULL)
    {
        return APP_UI_MAIN_TASK_WAITING;
    }
    if (strcmp(text, "task: active") == 0)
    {
        return APP_UI_MAIN_TASK_ACTIVE;
    }
    if (strcmp(text, "task: configured / dist gate on") == 0 ||
        strcmp(text, "task: configured / demo loose") == 0)
    {
        return APP_UI_MAIN_TASK_CONFIGURED;
    }
    if (strcmp(text, "task: camera start failed") == 0)
    {
        return APP_UI_MAIN_TASK_CAMERA_FAILED;
    }
    if (strcmp(text, "task: local mode / cloud start failed") == 0 ||
        strcmp(text, "task: local mode / cloud offline") == 0)
    {
        return APP_UI_MAIN_TASK_LOCAL_WAIT;
    }
    if (strcmp(text, "weather blocked") == 0 ||
        strcmp(text, "天气故障") == 0 ||
        strcmp(text, "禁止接驳") == 0)
    {
        return APP_UI_MAIN_TASK_WEATHER_BLOCKED;
    }
    return APP_UI_MAIN_TASK_WAITING;
}
// 根据任务枚举更新主屏任务文案、颜色和告警闪烁。
static void app_ui_apply_main_task_state_value_unlocked(app_ui_main_task_state_t state)
{
    s_main_task_state = state;
    const char *display_text = "等待任务";
    lv_color_t color = lv_color_hex(0x0F766E);
    bool blink = false;
    bool weather_blocked = false;
    switch (state) {
    case APP_UI_MAIN_TASK_ACTIVE:
        display_text = "任务运行";
        break;
    case APP_UI_MAIN_TASK_CONFIGURED:
        display_text = "等待任务";
        break;
    case APP_UI_MAIN_TASK_LOCAL_WAIT:
    case APP_UI_MAIN_TASK_CAMERA_FAILED:
        display_text = "本地等待";
        color = lv_color_hex(0xD97706);
        break;
    case APP_UI_MAIN_TASK_COMPLETED:
        display_text = "任务完成";
        break;
    case APP_UI_MAIN_TASK_WEATHER_BLOCKED:
        display_text = "恶劣天气";
        color = lv_color_hex(0xB91C1C);
        blink = true;
        weather_blocked = true;
        break;
    case APP_UI_MAIN_TASK_WAITING:
    default:
        break;
    }
    app_ui_apply_main_task_display_unlocked(display_text, color, blink, weather_blocked);
}
static void app_ui_apply_main_task_state_unlocked(const char *text)
{
    if (text == NULL)
    {
        return;
    }
    if (strcmp(text, "waiting task") == 0 ||
        strcmp(text, "task: active") == 0 ||
        strcmp(text, "task: configured / dist gate on") == 0 ||
        strcmp(text, "task: configured / demo loose") == 0 ||
        strcmp(text, "task: camera start failed") == 0 ||
        strcmp(text, "task: local mode / cloud start failed") == 0 ||
        strcmp(text, "task: local mode / cloud offline") == 0 ||
        strcmp(text, "weather blocked") == 0 ||
        strcmp(text, "天气故障") == 0 ||
        strcmp(text, "禁止接驳") == 0)
    {
        app_ui_apply_main_task_state_value_unlocked(app_ui_main_task_state_from_text(text));
        return;
    }
    app_ui_apply_main_task_display_unlocked(text, lv_color_hex(0x0F766E), false, false);
}

/* ---------- 异常演示界面 ---------- */

static const char *app_ui_exception_ch32_stage_text(int stage)
{
    switch (stage) {
    case 1: return "CH32空闲";
    case 2: return "CH32就绪";
    case 3: return "外门打开中";
    case 4: return "外门已打开";
    case 5: return "托盘伸出中";
    case 6: return "托盘已伸出";
    case 7: return "等待异常触发";
    case 8: return "检测到货物";
    case 9: return "托盘回收中";
    case 10: return "托盘已回收";
    case 11: return "外门关闭中";
    case 12: return "安全锁定";
    case 13: return "流程完成";
    case 14: return "CH32故障";
    default: return "等待CH32状态";
    }
}

static int app_ui_exception_phase_from_stage(int stage)
{
    if (stage >= 9 && stage <= 13)
    {
        return 3;
    }
    if (stage >= 5 && stage <= 8)
    {
        return 2;
    }
    if (stage >= 3 && stage <= 4)
    {
        return 1;
    }
    return 0;
}

static void app_ui_exception_phase_style_unlocked(int index,
    bool done,
    bool active,
    bool danger)
{
    if (index < 0 || index >= UI_EXCEPTION_PHASE_COUNT ||
        s_exception_phase_box[index] == NULL ||
        s_exception_phase_label[index] == NULL)
    {
        return;
    }
    const lv_color_t bg =
        danger ? lv_color_hex(0xFEF2F2) :
        active ? lv_color_hex(0xE0F2FE) :
        done ? lv_color_hex(0xECFDF5) :
        lv_color_hex(0xF8FAFC);
    const lv_color_t border =
        danger ? lv_color_hex(0xFCA5A5) :
        active ? lv_color_hex(0x7DD3FC) :
        done ? lv_color_hex(0x5EEAD4) :
        lv_color_hex(0xE2E8F0);
    const lv_color_t text =
        danger ? lv_color_hex(0x991B1B) :
        active ? lv_color_hex(0x0369A1) :
        done ? lv_color_hex(0x0F766E) :
        lv_color_hex(0x475569);
    lv_obj_set_style_bg_color(s_exception_phase_box[index], bg, 0);
    lv_obj_set_style_border_color(s_exception_phase_box[index], border, 0);
    lv_obj_set_style_text_color(s_exception_phase_label[index], text, 0);
}

static void app_ui_exception_apply_phase_unlocked(int active_phase,
    bool complete,
    bool danger)
{
    if (active_phase < 0)
    {
        active_phase = 0;
    }
    if (active_phase >= UI_EXCEPTION_PHASE_COUNT)
    {
        active_phase = UI_EXCEPTION_PHASE_COUNT - 1;
    }
    for (int i = 0; i < UI_EXCEPTION_PHASE_COUNT; i++)
    {
        app_ui_exception_phase_style_unlocked(i,
            complete || i < active_phase,
            !complete && i == active_phase,
            danger && i == active_phase);
    }
}

static void app_ui_exception_update_ch32_label_unlocked(void)
{
    if (s_exception_ch32_label == NULL)
    {
        return;
    }
    char buf[80] = {0};
    if (s_exception_last_error != 0U)
    {
        snprintf(buf,
            sizeof(buf),
            "%s / 错误码 %u",
            app_ui_exception_ch32_stage_text(s_exception_last_stage),
            (unsigned)s_exception_last_error);
    }
    else
    {
        snprintf(buf,
            sizeof(buf),
            "CH32：%s",
            app_ui_exception_ch32_stage_text(s_exception_last_stage));
    }
    lv_label_set_text(s_exception_ch32_label, buf);
}

static void app_ui_exception_apply_state_unlocked(app_ui_exception_demo_state_t state)
{
    s_exception_state = state;
    const char *status = "准备异常演示";
    const char *detail = "不启用摄像头，直接进入外门与托盘演示。";
    lv_color_t color = lv_color_hex(0x0F766E);
    int active_phase = app_ui_exception_phase_from_stage(s_exception_last_stage);
    bool complete = false;
    bool danger = false;
    const char *weather_button_text = "模拟天气";
    bool weather_button_enabled = true;
    const char *back_button_text = "返回主界面";
    bool back_button_visible = false;

    switch (state) {
    case APP_UI_EXCEPTION_DEMO_STARTING:
        status = "启动异常演示";
        detail = "正在向CH32发送接驳动作命令。";
        active_phase = 0;
        break;
    case APP_UI_EXCEPTION_DEMO_RUNNING:
        status = "机构演示中";
        detail = "外门与托盘动作中，可随时触发模拟天气。";
        break;
    case APP_UI_EXCEPTION_DEMO_WEATHER:
        status = "天气异常触发";
        detail = "恶劣天气已触发，托盘回收，外门关闭中。";
        color = lv_color_hex(0xB91C1C);
        active_phase = 3;
        weather_button_text = "回收中";
        weather_button_enabled = false;
        app_ui_exception_set_weather_unlocked(true);
        break;
    case APP_UI_EXCEPTION_DEMO_SAFE:
        status = "已安全回收";
        detail = "机构已安全锁定，可回到主界面。";
        complete = true;
        weather_button_text = "已回收";
        weather_button_enabled = false;
        back_button_text = "回到主界面";
        back_button_visible = true;
        break;
    case APP_UI_EXCEPTION_DEMO_FAILED:
        status = "演示异常";
        detail = "命令未被确认或CH32上报故障，请检查从控通信。";
        color = lv_color_hex(0xB91C1C);
        danger = true;
        weather_button_enabled = false;
        back_button_text = "回到主界面";
        back_button_visible = true;
        break;
    case APP_UI_EXCEPTION_DEMO_CH32_OFFLINE:
        status = "CH32未就绪";
        detail = "从控通信未就绪，暂不发送机械动作。";
        color = lv_color_hex(0xB91C1C);
        danger = true;
        weather_button_enabled = false;
        back_button_text = "回到主界面";
        back_button_visible = true;
        break;
    case APP_UI_EXCEPTION_DEMO_READY:
    default:
        active_phase = 0;
        back_button_visible = true;
        app_ui_exception_set_weather_unlocked(false);
        break;
    }

    if (s_exception_status_label != NULL)
    {
        lv_label_set_text(s_exception_status_label, status);
        lv_obj_set_style_text_color(s_exception_status_label, color, 0);
    }
    if (s_exception_detail_label != NULL)
    {
        lv_label_set_text(s_exception_detail_label, detail);
    }
    app_ui_exception_update_ch32_label_unlocked();
    app_ui_exception_apply_phase_unlocked(active_phase, complete, danger);
    app_ui_exception_set_weather_button_unlocked(weather_button_text, weather_button_enabled);
    app_ui_exception_set_back_button_unlocked(back_button_text, back_button_visible);
}

static lv_obj_t *app_ui_exception_add_button(lv_obj_t *parent,
    const char *text,
    lv_color_t color,
    int32_t w,
    int32_t h)
{
    lv_obj_t *btn = app_ui_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_width(label, w - 12);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &font_main_title_cn, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (strcmp(text, "模拟天气") == 0)
    {
        s_exception_weather_btn_label = label;
    }
    else if (strcmp(text, "返回主界面") == 0 || strcmp(text, "回到主界面") == 0)
    {
        s_exception_back_label = label;
    }
    return btn;
}

static void app_ui_create_exception_screen_unlocked(lv_obj_t *scr)
{
    s_exception_layer = lv_obj_create(scr);
    lv_obj_set_size(s_exception_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_color(s_exception_layer, lv_color_hex(0xF7F9FC), 0);
    lv_obj_set_style_bg_grad_color(s_exception_layer, lv_color_hex(0xEEF8F7), 0);
    lv_obj_set_style_bg_grad_dir(s_exception_layer, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_exception_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_exception_layer, 0, 0);
    lv_obj_set_style_radius(s_exception_layer, 0, 0);
    lv_obj_set_style_pad_all(s_exception_layer, 0, 0);
    lv_obj_clear_flag(s_exception_layer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo_img = lv_image_create(s_exception_layer);
    lv_image_set_src(logo_img, &logo);
    lv_image_set_scale(logo_img, 104);
    lv_obj_align(logo_img, LV_ALIGN_TOP_LEFT, 88, 12);

    lv_obj_t *title = lv_label_create(s_exception_layer);
    lv_obj_set_width(title, 660);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(title, &font_main_title_cn, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "异常演示");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    const int32_t content_x = (BSP_LCD_H_RES - 880) / 2;
    lv_obj_t *status_card = app_ui_create_soft_panel(s_exception_layer, 612, 126, 8);
    lv_obj_align(status_card, LV_ALIGN_TOP_LEFT, content_x, 126);
    s_exception_status_label = lv_label_create(status_card);
    lv_obj_set_width(s_exception_status_label, 520);
    lv_obj_set_style_text_color(s_exception_status_label, lv_color_hex(0x0F766E), 0);
    lv_obj_set_style_text_font(s_exception_status_label, &font_main_title_cn, 0);
    lv_label_set_text(s_exception_status_label, "准备异常演示");
    lv_obj_align(s_exception_status_label, LV_ALIGN_TOP_LEFT, 34, 16);
    s_exception_detail_label = lv_label_create(status_card);
    lv_obj_set_width(s_exception_detail_label, 544);
#if LVGL_VERSION_MAJOR >= 9
    lv_label_set_long_mode(s_exception_detail_label, LV_LABEL_LONG_MODE_DOTS);
#else
    lv_label_set_long_mode(s_exception_detail_label, LV_LABEL_LONG_DOT);
#endif
    lv_obj_set_style_text_color(s_exception_detail_label, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(s_exception_detail_label, &font_loading_cn, 0);
    lv_label_set_text(s_exception_detail_label, "不启用摄像头，直接进入外门与托盘演示。");
    lv_obj_align(s_exception_detail_label, LV_ALIGN_TOP_LEFT, 34, 54);
    s_exception_ch32_label = lv_label_create(status_card);
    lv_obj_set_width(s_exception_ch32_label, 544);
    lv_obj_set_style_text_color(s_exception_ch32_label, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(s_exception_ch32_label, &font_loading_cn, 0);
    lv_label_set_text(s_exception_ch32_label, "CH32：等待CH32状态");
    lv_obj_align(s_exception_ch32_label, LV_ALIGN_TOP_LEFT, 34, 88);

    lv_obj_t *weather_card = app_ui_create_soft_panel(s_exception_layer, 240, 126, 8);
    lv_obj_align(weather_card, LV_ALIGN_TOP_LEFT, content_x + 640, 126);
    s_exception_weather_title = lv_label_create(weather_card);
    lv_obj_set_width(s_exception_weather_title, 196);
    lv_obj_set_style_text_color(s_exception_weather_title, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(s_exception_weather_title, &font_main_title_cn, 0);
    lv_label_set_text(s_exception_weather_title, "演示天气");
    lv_obj_align(s_exception_weather_title, LV_ALIGN_TOP_LEFT, 22, 14);
    s_exception_weather_icon = lv_image_create(weather_card);
    lv_image_set_src(s_exception_weather_icon, app_ui_weather_image_src(s_exception_weather_code));
    lv_obj_align(s_exception_weather_icon, LV_ALIGN_TOP_LEFT, 24, 54);
    s_exception_weather_value_label = lv_label_create(weather_card);
    lv_obj_set_width(s_exception_weather_value_label, 104);
#if LVGL_VERSION_MAJOR >= 9
    lv_label_set_long_mode(s_exception_weather_value_label, LV_LABEL_LONG_MODE_WRAP);
#else
    lv_label_set_long_mode(s_exception_weather_value_label, LV_LABEL_LONG_WRAP);
#endif
    lv_obj_set_style_text_color(s_exception_weather_value_label, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(s_exception_weather_value_label, &font_main_title_cn, 0);
    lv_obj_set_style_text_line_space(s_exception_weather_value_label, 6, 0);
    lv_label_set_text(s_exception_weather_value_label, s_exception_weather_text);
    lv_obj_align(s_exception_weather_value_label, LV_ALIGN_TOP_LEFT, 116, 44);

    lv_obj_t *phase_card = app_ui_create_soft_panel(s_exception_layer, 880, 126, 8);
    lv_obj_align(phase_card, LV_ALIGN_TOP_MID, 0, 276);
    static const char *phase_texts[] = {
        "启动",
        "外门打开",
        "托盘伸出",
        "安全回收",
    };
    const int32_t phase_w = 184;
    const int32_t phase_h = 58;
    const int32_t phase_gap = 28;
    for (int i = 0; i < UI_EXCEPTION_PHASE_COUNT; i++)
    {
        lv_obj_t *phase = lv_obj_create(phase_card);
        s_exception_phase_box[i] = phase;
        lv_obj_set_size(phase, phase_w, phase_h);
        lv_obj_set_style_bg_color(phase, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_bg_opa(phase, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(phase, 1, 0);
        lv_obj_set_style_border_color(phase, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_radius(phase, 6, 0);
        lv_obj_set_style_pad_all(phase, 0, 0);
        lv_obj_clear_flag(phase, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(phase, LV_ALIGN_TOP_LEFT, 34 + i * (phase_w + phase_gap), 34);
        lv_obj_t *label = lv_label_create(phase);
        s_exception_phase_label[i] = label;
        lv_obj_set_width(label, phase_w - 12);
        lv_obj_set_style_text_color(label, lv_color_hex(0x475569), 0);
        lv_obj_set_style_text_font(label, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, phase_texts[i]);
        lv_obj_center(label);
    }

    lv_obj_t *guard_card = app_ui_create_soft_panel(s_exception_layer, 880, 110, 8);
    lv_obj_align(guard_card, LV_ALIGN_TOP_MID, 0, 426);
    lv_obj_t *guard_title = lv_label_create(guard_card);
    lv_obj_set_style_text_color(guard_title, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(guard_title, &font_main_title_cn, 0);
    lv_label_set_text(guard_title, "天气保护");
    lv_obj_align(guard_title, LV_ALIGN_TOP_LEFT, 34, 16);
    lv_obj_t *guard_hint = lv_label_create(guard_card);
    lv_obj_set_width(guard_hint, 500);
    lv_obj_set_height(guard_hint, 52);
    lv_obj_set_style_text_color(guard_hint, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(guard_hint, &font_loading_cn, 0);
    lv_obj_set_style_text_line_space(guard_hint, 4, 0);
    lv_label_set_text(guard_hint, "托盘动作期间点模拟天气\n立即回收托盘，外门关闭");
    lv_obj_align(guard_hint, LV_ALIGN_TOP_LEFT, 34, 52);
    s_exception_weather_btn = app_ui_exception_add_button(guard_card,
        "模拟天气",
        lv_color_hex(0xB91C1C),
        132,
        48);
    lv_obj_align(s_exception_weather_btn, LV_ALIGN_TOP_LEFT, 584, 30);
    lv_obj_add_event_cb(s_exception_weather_btn, app_ui_weather_sim_event_cb, LV_EVENT_CLICKED, NULL);
    s_exception_back_btn = app_ui_exception_add_button(guard_card,
        "返回主界面",
        lv_color_hex(0x64748B),
        132,
        48);
    lv_obj_align(s_exception_back_btn, LV_ALIGN_TOP_LEFT, 732, 30);
    lv_obj_add_event_cb(s_exception_back_btn, app_ui_exception_back_event_cb, LV_EVENT_CLICKED, NULL);
}

// 创建并显示主屏；已创建时仅取消隐藏并刷新状态。
/* ---------- 主屏对象创建与公共更新接口 ---------- */

bool app_ui_show_main_screen(void)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }
    lv_obj_t *scr = app_get_active_screen();
    if (s_exception_layer != NULL)
    {
        lv_obj_add_flag(s_exception_layer, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_main_layer == NULL)
    {
        s_main_layer = lv_obj_create(scr);
        lv_obj_set_size(s_main_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_set_style_bg_color(s_main_layer, lv_color_hex(0xF7F9FC), 0);
        lv_obj_set_style_bg_grad_color(s_main_layer, lv_color_hex(0xEFF5F8), 0);
        lv_obj_set_style_bg_grad_dir(s_main_layer, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(s_main_layer, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_main_layer, 0, 0);
        lv_obj_set_style_radius(s_main_layer, 0, 0);
        lv_obj_set_style_pad_all(s_main_layer, 0, 0);
        lv_obj_clear_flag(s_main_layer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_main_layer, LV_ALIGN_CENTER, 0, 0);
        lv_obj_t *main_logo = lv_image_create(s_main_layer);
        lv_image_set_src(main_logo, &logo);
        lv_image_set_scale(main_logo, 150);
        lv_obj_align(main_logo, LV_ALIGN_TOP_LEFT, -27, -16);
        lv_obj_t *header_line = lv_obj_create(s_main_layer);
        lv_obj_set_size(header_line, BSP_LCD_H_RES, 1);
        lv_obj_set_style_bg_color(header_line, lv_color_hex(0xE5E7EB), 0);
        lv_obj_set_style_bg_opa(header_line, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(header_line, 0, 0);
        lv_obj_set_style_radius(header_line, 0, 0);
        lv_obj_set_style_pad_all(header_line, 0, 0);
        lv_obj_clear_flag(header_line, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(header_line, LV_ALIGN_TOP_LEFT, 0, 92);
        lv_obj_t *proj_name = lv_label_create(s_main_layer);
        lv_obj_set_width(proj_name, BSP_LCD_H_RES - 48);
#if LVGL_VERSION_MAJOR >= 9
        lv_label_set_long_mode(proj_name, LV_LABEL_LONG_MODE_DOTS);
#else
        lv_label_set_long_mode(proj_name, LV_LABEL_LONG_DOT);
#endif
        lv_obj_set_style_text_color(proj_name, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_text_font(proj_name, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(proj_name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(proj_name, UI_PROJECT_NAME);
        lv_obj_align(proj_name, LV_ALIGN_TOP_MID, 0, 40);
        const int32_t task_card_x = 50;
        const int32_t task_card_y = 124;
        const int32_t task_card_w = 552;
        const int32_t task_card_h = 268;
        const int32_t side_card_x = 622;
        const int32_t side_card_w = 352;
        const int32_t side_card_h = 126;
        const int32_t strip_x = 50;
        const int32_t strip_y = 410;
        const int32_t strip_w = 924;
        const int32_t strip_h = 96;
        lv_obj_t *task_card = app_ui_create_soft_panel(s_main_layer,
            task_card_w,
            task_card_h,
            8);
        lv_obj_align(task_card, LV_ALIGN_TOP_LEFT, task_card_x, task_card_y);
        lv_obj_t *task_accent = lv_obj_create(task_card);
        s_main_task_accent = task_accent;
        lv_obj_set_size(task_accent, 6, task_card_h);
        lv_obj_set_style_bg_color(task_accent, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_bg_opa(task_accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(task_accent, 0, 0);
        lv_obj_set_style_radius(task_accent, 8, 0);
        lv_obj_set_style_pad_all(task_accent, 0, 0);
        lv_obj_clear_flag(task_accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(task_accent, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *task_title = lv_label_create(task_card);
        lv_obj_set_style_text_color(task_title, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(task_title, &font_main_title_cn, 0);
        lv_label_set_text(task_title, "任务中心");
        lv_obj_align(task_title, LV_ALIGN_TOP_LEFT, 28, 18);
        lv_obj_t *task_badge = lv_label_create(task_card);
        s_main_task_badge = task_badge;
        lv_obj_set_width(task_badge, 260);
#if LVGL_VERSION_MAJOR >= 9
        lv_label_set_long_mode(task_badge, LV_LABEL_LONG_MODE_DOTS);
#else
        lv_label_set_long_mode(task_badge, LV_LABEL_LONG_DOT);
#endif
        lv_obj_set_style_text_color(task_badge, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_text_font(task_badge, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(task_badge, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(task_badge, "状态同步中");
        lv_obj_align(task_badge, LV_ALIGN_TOP_RIGHT, -28, 18);
        s_main_task_label = lv_label_create(task_card);
        lv_obj_set_width(s_main_task_label, task_card_w - 56);
#if LVGL_VERSION_MAJOR >= 9
        lv_label_set_long_mode(s_main_task_label, LV_LABEL_LONG_MODE_DOTS);
#else
        lv_label_set_long_mode(s_main_task_label, LV_LABEL_LONG_DOT);
#endif
        lv_obj_set_style_text_color(s_main_task_label, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_text_font(s_main_task_label, &font_main_title_cn, 0);
        lv_label_set_text(s_main_task_label, "等待任务");
        lv_obj_align(s_main_task_label, LV_ALIGN_TOP_LEFT, 28, 66);
        lv_obj_t *task_hint = lv_label_create(task_card);
        s_main_task_hint_label = task_hint;
        lv_obj_set_width(task_hint, task_card_w - 56);
        lv_obj_set_style_text_color(task_hint, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(task_hint, &font_loading_cn, 0);
        lv_label_set_text(task_hint, "云端任务 / 视觉接驳");
        lv_obj_align(task_hint, LV_ALIGN_TOP_LEFT, 28, 108);
        s_main_task_alert = lv_obj_create(task_card);
        lv_obj_set_size(s_main_task_alert, 176, 34);
        lv_obj_set_style_bg_color(s_main_task_alert, lv_color_hex(0xFEF2F2), 0);
        lv_obj_set_style_bg_opa(s_main_task_alert, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_main_task_alert, 1, 0);
        lv_obj_set_style_border_color(s_main_task_alert, lv_color_hex(0xFCA5A5), 0);
        lv_obj_set_style_radius(s_main_task_alert, 6, 0);
        lv_obj_set_style_pad_all(s_main_task_alert, 0, 0);
        lv_obj_clear_flag(s_main_task_alert, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_main_task_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_main_task_alert, LV_ALIGN_TOP_LEFT, 28, 104);
        s_main_task_alert_label = lv_label_create(s_main_task_alert);
        lv_obj_set_width(s_main_task_alert_label, 156);
        lv_obj_set_style_text_color(s_main_task_alert_label, lv_color_hex(0x991B1B), 0);
        lv_obj_set_style_text_font(s_main_task_alert_label, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(s_main_task_alert_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_main_task_alert_label, "禁止接驳");
        lv_obj_center(s_main_task_alert_label);
        lv_obj_t *task_divider = lv_obj_create(task_card);
        lv_obj_set_size(task_divider, task_card_w - 56, 1);
        lv_obj_set_style_bg_color(task_divider, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_opa(task_divider, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(task_divider, 0, 0);
        lv_obj_set_style_radius(task_divider, 0, 0);
        lv_obj_set_style_pad_all(task_divider, 0, 0);
        lv_obj_clear_flag(task_divider, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(task_divider, LV_ALIGN_TOP_LEFT, 28, 148);
        static const char *phase_texts[] = {
            "云端",
            "通信",
            "接驳",
        };
        const int32_t phase_w = 112;
        const int32_t phase_h = 34;
        const int32_t phase_gap = 14;
        const int32_t phase_y = 178;
        for (int i = 0; i < UI_MAIN_PHASE_COUNT; i++)
        {
            lv_obj_t *phase = lv_obj_create(task_card);
            s_main_phase_box[i] = phase;
            lv_obj_set_size(phase, phase_w, phase_h);
            lv_obj_set_style_bg_color(phase, lv_color_hex(0xF8FAFC), 0);
            lv_obj_set_style_bg_opa(phase, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(phase, 1, 0);
            lv_obj_set_style_border_color(phase, lv_color_hex(0xE2E8F0), 0);
            lv_obj_set_style_radius(phase, 6, 0);
            lv_obj_set_style_pad_all(phase, 0, 0);
            lv_obj_clear_flag(phase, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(phase, LV_ALIGN_TOP_LEFT, 28 + i * (phase_w + phase_gap), phase_y);
            lv_obj_t *phase_label = lv_label_create(phase);
            s_main_phase_label[i] = phase_label;
            lv_obj_set_width(phase_label, phase_w - 12);
            lv_obj_set_style_text_color(phase_label, lv_color_hex(0x475569), 0);
            lv_obj_set_style_text_font(phase_label, &font_loading_cn, 0);
            lv_obj_set_style_text_align(phase_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(phase_label, phase_texts[i]);
            lv_obj_center(phase_label);
        }
        lv_obj_t *task_foot = lv_label_create(task_card);
        lv_obj_set_width(task_foot, task_card_w - 56);
        lv_obj_set_style_text_color(task_foot, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(task_foot, &font_loading_cn, 0);
        lv_label_set_text(task_foot, "云端 / 通信 / 接驳 LED状态");
        lv_obj_align(task_foot, LV_ALIGN_BOTTOM_LEFT, 28, -16);
        lv_obj_t *clock_card = app_ui_create_soft_panel(s_main_layer,
            side_card_w,
            side_card_h,
            8);
        lv_obj_align(clock_card, LV_ALIGN_TOP_LEFT, side_card_x, task_card_y);
        lv_obj_t *clock_title = lv_label_create(clock_card);
        lv_obj_set_style_text_color(clock_title, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(clock_title, &font_main_title_cn, 0);
        lv_label_set_text(clock_title, "北京时间");
        lv_obj_align(clock_title, LV_ALIGN_TOP_LEFT, 22, 14);
        s_main_clock_time = lv_label_create(clock_card);
        lv_obj_set_width(s_main_clock_time, side_card_w - 44);
        lv_obj_set_style_text_color(s_main_clock_time, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_text_font(s_main_clock_time, &font_title_en, 0);
        lv_obj_set_style_text_align(s_main_clock_time, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_letter_space(s_main_clock_time, 0, 0);
        lv_label_set_text(s_main_clock_time, "--:--:--");
        lv_obj_align(s_main_clock_time, LV_ALIGN_TOP_MID, 0, 42);
        s_main_clock_note = lv_label_create(clock_card);
        lv_obj_set_width(s_main_clock_note, side_card_w - 44);
        lv_obj_set_style_text_color(s_main_clock_note, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(s_main_clock_note, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(s_main_clock_note, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_main_clock_note, "同步中");
        lv_obj_align(s_main_clock_note, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_t *weather_card = app_ui_create_soft_panel(s_main_layer,
            side_card_w,
            side_card_h,
            8);
        lv_obj_align(weather_card,
            LV_ALIGN_TOP_LEFT,
            side_card_x,
            task_card_y + side_card_h + 16);
        s_main_weather_title = lv_label_create(weather_card);
        lv_obj_set_width(s_main_weather_title, side_card_w - 44);
#if LVGL_VERSION_MAJOR >= 9
        lv_label_set_long_mode(s_main_weather_title, LV_LABEL_LONG_MODE_DOTS);
#else
        lv_label_set_long_mode(s_main_weather_title, LV_LABEL_LONG_DOT);
#endif
        lv_obj_set_style_text_color(s_main_weather_title, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(s_main_weather_title, &font_main_title_cn, 0);
        lv_label_set_text(s_main_weather_title, "云端天气");
        lv_obj_align(s_main_weather_title, LV_ALIGN_TOP_LEFT, 22, 14);
        s_main_weather_icon = lv_image_create(weather_card);
        lv_image_set_src(s_main_weather_icon, app_ui_weather_image_src(s_main_weather_code));
        lv_obj_align(s_main_weather_icon, LV_ALIGN_TOP_LEFT, 24, 52);
        s_main_weather_label = lv_label_create(weather_card);
        lv_obj_set_width(s_main_weather_label, side_card_w - 136);
#if LVGL_VERSION_MAJOR >= 9
        lv_label_set_long_mode(s_main_weather_label, LV_LABEL_LONG_MODE_WRAP);
#else
        lv_label_set_long_mode(s_main_weather_label, LV_LABEL_LONG_WRAP);
#endif
        lv_obj_set_style_text_color(s_main_weather_label, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_text_font(s_main_weather_label, &font_main_title_cn, 0);
        lv_obj_set_style_text_align(s_main_weather_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_line_space(s_main_weather_label, 6, 0);
        lv_label_set_text(s_main_weather_label, s_main_weather_text);
        lv_obj_align(s_main_weather_label, LV_ALIGN_TOP_LEFT, 118, 44);
        lv_obj_t *link_bar = app_ui_create_soft_panel(s_main_layer,
            strip_w,
            strip_h,
            8);
        lv_obj_align(link_bar, LV_ALIGN_TOP_LEFT, strip_x, strip_y);
        lv_obj_t *link_title = lv_label_create(link_bar);
        lv_obj_set_style_text_color(link_title, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(link_title, &font_main_title_cn, 0);
        lv_label_set_text(link_title, "通信状态");
        lv_obj_align(link_title, LV_ALIGN_TOP_LEFT, 24, 14);
        s_main_wifi_ind = app_ui_create_status_dot(link_bar, "Wi-Fi", 154, 26);
        s_main_mqtt_ind = app_ui_create_status_dot(link_bar, "MQTT", 282, 26);
        s_main_ch32_ind = app_ui_create_status_dot(link_bar, "CH32", 416, 26);
        s_main_conn_label = lv_label_create(link_bar);
        lv_obj_set_width(s_main_conn_label, 210);
        lv_obj_set_style_text_color(s_main_conn_label, lv_color_hex(0xD85B5B), 0);
        lv_obj_set_style_text_font(s_main_conn_label, &font_loading_cn, 0);
        lv_label_set_text(s_main_conn_label, "通信未就绪");
        lv_obj_align(s_main_conn_label, LV_ALIGN_TOP_LEFT, 154, 58);
        s_main_dock_label = lv_label_create(link_bar);
        lv_obj_set_width(s_main_dock_label, 150);
        lv_obj_set_style_text_color(s_main_dock_label, lv_color_hex(0xD85B5B), 0);
        lv_obj_set_style_text_font(s_main_dock_label, &font_loading_cn, 0);
        lv_obj_set_style_text_align(s_main_dock_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(s_main_dock_label, "接驳未就绪");
        lv_obj_align(s_main_dock_label, LV_ALIGN_TOP_LEFT, 416, 58);
        lv_obj_t *link_divider = lv_obj_create(link_bar);
        lv_obj_set_size(link_divider, 1, strip_h - 28);
        lv_obj_set_style_bg_color(link_divider, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_opa(link_divider, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(link_divider, 0, 0);
        lv_obj_set_style_radius(link_divider, 0, 0);
        lv_obj_set_style_pad_all(link_divider, 0, 0);
        lv_obj_clear_flag(link_divider, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(link_divider, LV_ALIGN_TOP_LEFT, 626, 14);
        lv_obj_t *action_title = lv_label_create(link_bar);
        lv_obj_set_style_text_color(action_title, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(action_title, &font_main_title_cn, 0);
        lv_label_set_text(action_title, "安全 / 语音");
        lv_obj_align(action_title, LV_ALIGN_TOP_LEFT, 654, 14);
        s_main_exception_btn = app_ui_button_create(link_bar);
        lv_obj_set_size(s_main_exception_btn, 122, 42);
        lv_obj_set_style_bg_color(s_main_exception_btn, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_bg_opa(s_main_exception_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_main_exception_btn, 0, 0);
        lv_obj_set_style_radius(s_main_exception_btn, 8, 0);
        lv_obj_set_style_pad_all(s_main_exception_btn, 0, 0);
        lv_obj_clear_flag(s_main_exception_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_main_exception_btn, LV_ALIGN_TOP_LEFT, 646, 44);
        lv_obj_add_event_cb(s_main_exception_btn, app_ui_exception_demo_event_cb, LV_EVENT_CLICKED, NULL);
        s_main_exception_label = lv_label_create(s_main_exception_btn);
        lv_obj_set_width(s_main_exception_label, 114);
        lv_obj_set_style_text_color(s_main_exception_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_main_exception_label, &font_button_cn, 0);
        lv_obj_set_style_text_align(s_main_exception_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_main_exception_label, "安全接管");
        lv_obj_center(s_main_exception_label);
        s_main_voice_btn = app_ui_button_create(link_bar);
        lv_obj_set_size(s_main_voice_btn, 132, 42);
        lv_obj_set_style_bg_color(s_main_voice_btn, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_bg_opa(s_main_voice_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_main_voice_btn, 0, 0);
        lv_obj_set_style_radius(s_main_voice_btn, 8, 0);
        lv_obj_set_style_pad_all(s_main_voice_btn, 0, 0);
        lv_obj_clear_flag(s_main_voice_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_main_voice_btn, LV_ALIGN_TOP_LEFT, 780, 44);
        lv_obj_add_event_cb(s_main_voice_btn, app_ui_voice_toggle_event_cb, LV_EVENT_CLICKED, NULL);
        s_main_voice_label = lv_label_create(s_main_voice_btn);
        lv_obj_set_width(s_main_voice_label, 124);
        lv_obj_set_style_text_color(s_main_voice_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_main_voice_label, &font_button_cn, 0);
        lv_obj_set_style_text_align(s_main_voice_label, LV_TEXT_ALIGN_CENTER, 0);
        app_ui_update_main_voice_button_unlocked();
        app_ui_apply_weather_unlocked();
        app_ui_apply_main_task_state_value_unlocked(s_main_weather_simulated ?
            APP_UI_MAIN_TASK_WEATHER_BLOCKED :
            s_main_task_state);
        app_ui_update_clock_unlocked();
        if (s_main_clock_timer == NULL)
        {
            s_main_clock_timer = lv_timer_create(app_ui_clock_timer_cb, 1000, NULL);
        }
        if (s_main_task_blink_timer == NULL)
        {
            s_main_task_blink_timer = lv_timer_create(app_ui_main_task_blink_timer_cb, UI_TASK_BLINK_MS, NULL);
            if (s_main_task_blink_enabled)
            {
                lv_timer_reset(s_main_task_blink_timer);
            }
            else
            {
                lv_timer_pause(s_main_task_blink_timer);
            }
        }
        lv_obj_t *contest = lv_label_create(s_main_layer);
        lv_obj_set_style_text_color(contest, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(contest, &font_loading_cn, 0);
        lv_obj_set_style_text_align(contest, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(contest, UI_CONTEST_NAME);
        lv_obj_align(contest, LV_ALIGN_BOTTOM_MID, 0, -12);
        lv_obj_t *team = lv_label_create(s_main_layer);
        lv_obj_set_style_text_color(team, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_font(team, &font_loading_cn, 0);
        lv_obj_set_style_text_align(team, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(team, UI_TEAM_NAME);
        lv_obj_align(team, LV_ALIGN_BOTTOM_MID, 0, -38);
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

bool app_ui_show_exception_demo_screen(void)
{
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return false;
    }
    lv_obj_t *scr = app_get_active_screen();
    if (s_exception_layer == NULL)
    {
        app_ui_create_exception_screen_unlocked(scr);
    }
    else
    {
        lv_obj_clear_flag(s_exception_layer, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_main_layer != NULL)
    {
        lv_obj_add_flag(s_main_layer, LV_OBJ_FLAG_HIDDEN);
    }
    s_exception_last_stage = 0;
    s_exception_last_error = 0;
    app_ui_exception_apply_state_unlocked(APP_UI_EXCEPTION_DEMO_READY);
    lv_obj_move_foreground(s_exception_layer);
    lv_refr_now(NULL);
    bsp_display_unlock();
    return true;
}

void app_ui_exception_demo_set_state(app_ui_exception_demo_state_t state)
{
    if (s_exception_layer == NULL)
    {
        s_exception_state = state;
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_exception_apply_state_unlocked(state);
    bsp_display_unlock();
}

void app_ui_exception_demo_update_ch32(int stage, uint8_t error)
{
    s_exception_last_stage = stage;
    s_exception_last_error = error;
    if (s_exception_layer == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    if (error != 0U || stage == 14)
    {
        app_ui_exception_apply_state_unlocked(APP_UI_EXCEPTION_DEMO_FAILED);
    }
    else if (stage == 12 || stage == 13)
    {
        app_ui_exception_apply_state_unlocked(APP_UI_EXCEPTION_DEMO_SAFE);
    }
    else if (stage >= 9 && stage <= 11)
    {
        app_ui_exception_apply_state_unlocked(APP_UI_EXCEPTION_DEMO_WEATHER);
    }
    else if (stage >= 3 && stage <= 8 &&
        s_exception_state != APP_UI_EXCEPTION_DEMO_WEATHER &&
        s_exception_state != APP_UI_EXCEPTION_DEMO_SAFE &&
        s_exception_state != APP_UI_EXCEPTION_DEMO_FAILED)
    {
        app_ui_exception_apply_state_unlocked(APP_UI_EXCEPTION_DEMO_RUNNING);
    }
    else
    {
        app_ui_exception_update_ch32_label_unlocked();
        app_ui_exception_apply_phase_unlocked(app_ui_exception_phase_from_stage(stage),
            false,
            false);
    }
    bsp_display_unlock();
}
// 隐藏主屏，进入相机预览时调用。
void app_ui_hide_main_screen(void)
{
    if (s_main_layer == NULL && s_exception_layer == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    if (s_main_layer != NULL)
    {
        lv_obj_add_flag(s_main_layer, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_exception_layer != NULL)
    {
        lv_obj_add_flag(s_exception_layer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_refr_now(NULL);
    bsp_display_unlock();
}
void app_ui_main_screen_set_voice_enabled(bool enabled)
{
    s_main_voice_enabled = enabled;
    if (s_main_voice_btn == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_update_main_voice_button_unlocked();
    bsp_display_unlock();
}
// 更新三路连接状态灯和阶段指示。
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
    s_main_status_seen = true;
    s_main_status_wifi_ok = wifi_ok;
    s_main_status_mqtt_ok = mqtt_ok;
    s_main_status_ch32_ok = ch32_ok;
    lv_color_t green = lv_color_hex(0x0F766E);
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
    app_ui_refresh_main_phase_indicators_unlocked();
    bsp_display_unlock();
}
// 按枚举设置任务状态，推荐新代码使用这个接口。
void app_ui_main_screen_set_task_state(app_ui_main_task_state_t state)
{
    s_main_task_state = state;
    if (s_main_task_label == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_apply_main_task_state_value_unlocked(state);
    bsp_display_unlock();
}
// 兼容旧文本状态入口，会尝试映射到主屏枚举。
void app_ui_main_screen_set_task_text(const char *text)
{
    if (text == NULL)
    {
        return;
    }
    s_main_task_state = app_ui_main_task_state_from_text(text);
    if (s_main_task_label == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_apply_main_task_state_unlocked(text);
    bsp_display_unlock();
}
// 单独刷新天气文字和图标。
void app_ui_main_screen_set_weather(const char *text, int weather_code)
{
    if (text == NULL)
    {
        return;
    }
    strlcpy(s_main_weather_text, text, sizeof(s_main_weather_text));
    s_main_weather_code = weather_code;
    if (s_main_weather_label == NULL && s_main_weather_icon == NULL)
    {
        return;
    }
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_apply_weather_unlocked();
    bsp_display_unlock();
}
// 更新天气模拟标记，并同步告警视觉状态。
void app_ui_main_screen_set_weather_simulated(bool simulated)
{
    s_main_weather_simulated = simulated;
    if (!bsp_display_lock(UI_LOCK_SHORT_MS))
    {
        return;
    }
    app_ui_apply_weather_unlocked();
    if (simulated)
    {
        app_ui_apply_main_task_state_value_unlocked(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
    }
    else
    {
        app_ui_set_main_task_blink_unlocked(false);
        app_ui_apply_main_task_visual_unlocked(false);
    }
    bsp_display_unlock();
}
// 一次性应用天气、模拟状态和可选任务文本，减少多次抢锁。
void app_ui_main_screen_apply_weather_state(const char *weather_text,
    int weather_code,
    bool simulated,
    const char *task_text)
{
    if (weather_text != NULL)
    {
        strlcpy(s_main_weather_text, weather_text, sizeof(s_main_weather_text));
        s_main_weather_code = weather_code;
    }
    s_main_weather_simulated = simulated;
    if (!bsp_display_lock(UI_LOCK_BOOT_MS))
    {
        return;
    }
    app_ui_apply_weather_unlocked();
    if (simulated && task_text == NULL)
    {
        app_ui_apply_main_task_state_value_unlocked(APP_UI_MAIN_TASK_WEATHER_BLOCKED);
    }
    else
    {
        app_ui_apply_main_task_state_unlocked(task_text);
    }
    if (!simulated && task_text == NULL)
    {
        app_ui_set_main_task_blink_unlocked(false);
        app_ui_apply_main_task_visual_unlocked(false);
    }
    bsp_display_unlock();
}
