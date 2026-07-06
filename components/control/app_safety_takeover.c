#include "app_safety_takeover.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_cloud.h"
#include "app_drone_ai.h"
#include "app_task.h"
#include "app_ui.h"

// 安全接管演练：复用正常任务的 AI/Tag/CH32 流程，在托盘伸出后追加离场监测。

static const char *TAG = "safety_takeover";

#define SAFETY_TASK_STACK              (5 * 1024)
#define SAFETY_TASK_PRIO               5
#define SAFETY_TASK_POLL_MS            200U
#define SAFETY_LOST_AFTER_MS           2400U
#define SAFETY_LOST_COUNTDOWN_MS       10000U
#define SAFETY_RETURN_ARM_MS           1000U
#define SAFETY_DONE_HOLD_MS            1500U
#define SAFETY_FAILED_HOLD_MS          3000U

typedef enum {
    SAFETY_PHASE_IDLE = 0,
    SAFETY_PHASE_STARTING,
    SAFETY_PHASE_DRONE_CONFIRMED,
    SAFETY_PHASE_WINDOW_OPEN,
    SAFETY_PHASE_LOST_COUNTDOWN,
    SAFETY_PHASE_RECOVERED,
    SAFETY_PHASE_TYPHOON,
    SAFETY_PHASE_SAFE_DONE,
    SAFETY_PHASE_FAILED,
} app_safety_phase_t;

typedef struct {
    bool active;
    bool ai_monitor_enabled;
    bool typhoon_task_running;
    uint16_t target_id;
    uint32_t task_generation;
    app_safety_phase_t phase;
    TickType_t countdown_deadline_tick;
    TickType_t done_tick;
    uint32_t monitor_start_ms;
    uint32_t countdown_start_ms;
    bool presence_seen;
    bool loss_countdown_consumed;
    bool recovered_ui_synced;
    int32_t last_countdown_s;
} app_safety_takeover_ctx_t;

static portMUX_TYPE s_safety_mux = portMUX_INITIALIZER_UNLOCKED;
static app_safety_takeover_ctx_t s_safety = {0};
static TaskHandle_t s_safety_task = NULL;
static bool s_task_callback_registered = false;

static TickType_t app_safety_ms_to_ticks(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

static uint32_t app_safety_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static int32_t app_safety_remaining_seconds(TickType_t now, TickType_t deadline)
{
    if ((int32_t)(deadline - now) <= 0)
    {
        return 0;
    }
    const TickType_t remain = deadline - now;
    return (int32_t)((remain + pdMS_TO_TICKS(999U)) / pdMS_TO_TICKS(1000U));
}

static void app_safety_copy_ctx(app_safety_takeover_ctx_t *out)
{
    taskENTER_CRITICAL(&s_safety_mux);
    *out = s_safety;
    taskEXIT_CRITICAL(&s_safety_mux);
}

static bool app_safety_task_is_owned(const app_task_snapshot_t *snap)
{
    if (snap == NULL ||
        snap->generation == 0U ||
        strcmp(snap->source, "safety") != 0)
    {
        return false;
    }

    bool owned = false;
    taskENTER_CRITICAL(&s_safety_mux);
    if (s_safety.active && snap->target_id == s_safety.target_id)
    {
        if (s_safety.task_generation == 0U && snap->active)
        {
            s_safety.task_generation = snap->generation;
        }
        owned = s_safety.task_generation == snap->generation;
    }
    taskEXIT_CRITICAL(&s_safety_mux);
    return owned;
}

static bool app_safety_current_task_is_owned(bool require_active)
{
    app_task_snapshot_t snap = {0};
    if (!app_task_peek_snapshot(&snap) || !app_safety_task_is_owned(&snap))
    {
        return false;
    }
    return !require_active || snap.active;
}

static uint16_t app_safety_force_idle(void)
{
    taskENTER_CRITICAL(&s_safety_mux);
    s_safety.active = false;
    s_safety.ai_monitor_enabled = false;
    s_safety.typhoon_task_running = false;
    s_safety.task_generation = 0U;
    s_safety.phase = SAFETY_PHASE_IDLE;
    s_safety.countdown_deadline_tick = 0;
    s_safety.done_tick = 0;
    s_safety.monitor_start_ms = 0;
    s_safety.countdown_start_ms = 0;
    s_safety.presence_seen = false;
    s_safety.loss_countdown_consumed = false;
    s_safety.recovered_ui_synced = false;
    s_safety.last_countdown_s = -1;
    const uint16_t target_id = s_safety.target_id;
    taskEXIT_CRITICAL(&s_safety_mux);
    return target_id;
}

static void app_safety_return_failed_to_main(void)
{
    app_ui_set_preview_hud_visible(false);
    app_ui_safety_takeover_set_visible(false);
    if (!app_ui_show_main_screen())
    {
        ESP_LOGW(TAG, "return to main screen failed, preview worker will retry");
    }
}

static unsigned app_safety_ai_confirm_total(const app_drone_ai_stats_t *ai)
{
    return (unsigned)((ai != NULL && ai->confirm_hits != 0U) ? ai->confirm_hits : 1U);
}

static unsigned app_safety_ai_hit_count(const app_drone_ai_stats_t *ai)
{
    const unsigned total = app_safety_ai_confirm_total(ai);
    unsigned hits = (unsigned)(ai != NULL ? ai->hit_count : 0U);
    return hits > total ? total : hits;
}

static void app_safety_format_ai_detail(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0)
    {
        return;
    }

    app_drone_ai_stats_t ai = {0};
    app_drone_ai_get_stats(&ai);
    const unsigned total = app_safety_ai_confirm_total(&ai);
    const unsigned hits = app_safety_ai_hit_count(&ai);

    if (ai.confirmed)
    {
        snprintf(buf, buf_len, "AI已确认 命中%u/%u", hits, total);
    }
    else if (ai.inferred == 0U)
    {
        snprintf(buf, buf_len, "AI推理等待首帧");
    }
    else
    {
        snprintf(buf,
            buf_len,
            "AI推理%lu帧 命中%u/%u",
            (unsigned long)ai.inferred,
            hits,
            total);
    }
}

static bool app_safety_set_ui(app_ui_safety_takeover_state_t state,
    int32_t countdown_s,
    uint16_t target_id)
{
    char detail_buf[96] = {0};
    const char *phase_text = "识别无人机";
    const char *status_title = "等待无人机进入画面";
    const char *status_detail = "AI推理等待首帧";

    switch (state) {
    case APP_UI_SAFETY_TAKEOVER_STARTING:
        phase_text = "摄像头启动";
        status_title = "安全接管准备";
        status_detail = "打开摄像头，准备识别目标";
        break;
    case APP_UI_SAFETY_TAKEOVER_WAIT_DRONE:
        phase_text = "识别无人机";
        status_title = "等待无人机进入画面";
        app_safety_format_ai_detail(detail_buf, sizeof(detail_buf));
        status_detail = detail_buf;
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_CONFIRMED:
        phase_text = "校验TAG身份";
        status_title = "无人机已识别";
        snprintf(detail_buf,
            sizeof(detail_buf),
            "目标TAG %u，等待视觉确认",
            (unsigned)target_id);
        status_detail = detail_buf;
        break;
    case APP_UI_SAFETY_TAKEOVER_AUTH_PASSED:
        phase_text = "身份通过";
        status_title = "身份通过";
        status_detail = "外门打开，托盘准备伸出";
        break;
    case APP_UI_SAFETY_TAKEOVER_WINDOW_OPEN:
        phase_text = "接驳窗口开启";
        status_title = "接驳窗口开启";
        status_detail = "重新识别无人机，离场后开始倒计时";
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_LOST:
        phase_text = "离场倒计时";
        status_title = "未检测到无人机";
        snprintf(detail_buf,
            sizeof(detail_buf),
            "剩余%lds，保持接驳窗口",
            (long)((countdown_s > 0) ? countdown_s : 1));
        status_detail = detail_buf;
        break;
    case APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED:
        phase_text = "目标返回";
        status_title = "无人机已返回";
        status_detail = "继续保持接驳窗口";
        break;
    case APP_UI_SAFETY_TAKEOVER_TYPHOON:
        phase_text = "天气保护";
        status_title = "安全回收中";
        status_detail = "托盘回收，外门关闭";
        break;
    case APP_UI_SAFETY_TAKEOVER_SAFE_DONE:
        phase_text = "安全闭环完成";
        status_title = "安全闭环完成";
        status_detail = "托盘已回收，外门已关闭";
        break;
    case APP_UI_SAFETY_TAKEOVER_FAILED:
        phase_text = "保护异常";
        status_title = "安全保护异常";
        status_detail = "请检查摄像头或从控通信";
        break;
    case APP_UI_SAFETY_TAKEOVER_IDLE:
    default:
        break;
    }

    app_ui_safety_takeover_view_t view = {
        .state = state,
        .countdown_s = countdown_s,
        .target_id = target_id,
        .phase_text = phase_text,
        .status_title = status_title,
        .status_detail = status_detail,
    };
    return app_ui_safety_takeover_set_view(&view);
}

static void app_safety_mark_window_open_locked(void)
{
    s_safety.phase = SAFETY_PHASE_WINDOW_OPEN;
    s_safety.ai_monitor_enabled = true;
    s_safety.countdown_deadline_tick = 0;
    s_safety.monitor_start_ms = app_safety_now_ms();
    s_safety.countdown_start_ms = 0;
    s_safety.presence_seen = false;
    s_safety.loss_countdown_consumed = false;
    s_safety.recovered_ui_synced = false;
    s_safety.last_countdown_s = -1;
}

static uint16_t app_safety_mark_recovered_locked(void)
{
    s_safety.ai_monitor_enabled = false;
    s_safety.phase = SAFETY_PHASE_RECOVERED;
    s_safety.countdown_deadline_tick = 0;
    s_safety.countdown_start_ms = 0;
    s_safety.presence_seen = true;
    s_safety.loss_countdown_consumed = true;
    s_safety.recovered_ui_synced = false;
    s_safety.last_countdown_s = -1;
    return s_safety.target_id;
}

static void app_safety_mark_recovered_ui_synced(void)
{
    taskENTER_CRITICAL(&s_safety_mux);
    if (s_safety.phase == SAFETY_PHASE_RECOVERED)
    {
        s_safety.recovered_ui_synced = true;
    }
    taskEXIT_CRITICAL(&s_safety_mux);
}

static bool app_safety_stage_tray_out(const app_ch32_line_t *msg)
{
    if (msg == NULL || msg->type != APP_CH32_LINE_PROTO_STATUS)
    {
        return false;
    }
    if ((msg->proto_flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U)
    {
        return true;
    }
    return msg->proto_stage == APP_CH32_STAGE_TRAY_EXTENDED ||
           msg->proto_stage == APP_CH32_STAGE_WAITING_CARGO;
}

static bool app_safety_stage_safe_done(const app_ch32_line_t *msg)
{
    if (msg == NULL || msg->type != APP_CH32_LINE_PROTO_STATUS)
    {
        return false;
    }
    return msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED ||
           msg->proto_stage == APP_CH32_STAGE_COMPLETE ||
           ((msg->proto_flags & APP_CH32_FLAG_LOCKED) != 0U);
}

static void app_safety_typhoon_task(void *arg)
{
    (void)arg;
    esp_err_t ret = app_cloud_trigger_weather_emergency_wait();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "typhoon protection failed: %s", esp_err_to_name(ret));
        taskENTER_CRITICAL(&s_safety_mux);
        s_safety.phase = SAFETY_PHASE_FAILED;
        s_safety.ai_monitor_enabled = false;
        s_safety.typhoon_task_running = false;
        s_safety.done_tick = xTaskGetTickCount();
        const uint16_t target_id = s_safety.target_id;
        taskEXIT_CRITICAL(&s_safety_mux);
        app_drone_ai_set_continuous(false);
        app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_FAILED, 0, target_id);
        vTaskDelete(NULL);
        return;
    }

    taskENTER_CRITICAL(&s_safety_mux);
    s_safety.typhoon_task_running = false;
    const uint16_t target_id = s_safety.target_id;
    taskEXIT_CRITICAL(&s_safety_mux);
    app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_TYPHOON, 0, target_id);
    vTaskDelete(NULL);
}

static void app_safety_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_safety_takeover_ctx_t ctx = {0};
        app_safety_copy_ctx(&ctx);
        const TickType_t now = xTaskGetTickCount();

        if (!ctx.active)
        {
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        if (ctx.phase == SAFETY_PHASE_SAFE_DONE || ctx.phase == SAFETY_PHASE_FAILED)
        {
            const uint32_t hold_ms = ctx.phase == SAFETY_PHASE_SAFE_DONE ?
                                     SAFETY_DONE_HOLD_MS :
                                     SAFETY_FAILED_HOLD_MS;
            if (ctx.done_tick != 0 &&
                (now - ctx.done_tick) >= app_safety_ms_to_ticks(hold_ms))
            {
                taskENTER_CRITICAL(&s_safety_mux);
                s_safety.active = false;
                s_safety.ai_monitor_enabled = false;
                s_safety.phase = SAFETY_PHASE_IDLE;
                taskEXIT_CRITICAL(&s_safety_mux);
                app_drone_ai_set_continuous(false);
                if (ctx.phase == SAFETY_PHASE_FAILED)
                {
                    app_safety_return_failed_to_main();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        if (ctx.phase == SAFETY_PHASE_STARTING && app_drone_ai_is_drone_confirmed())
        {
            taskENTER_CRITICAL(&s_safety_mux);
            if (s_safety.phase == SAFETY_PHASE_STARTING)
            {
                s_safety.phase = SAFETY_PHASE_DRONE_CONFIRMED;
            }
            const uint16_t target_id = s_safety.target_id;
            taskEXIT_CRITICAL(&s_safety_mux);
            app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_CONFIRMED, 0, target_id);
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        if (ctx.phase == SAFETY_PHASE_STARTING)
        {
            app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_WAIT_DRONE, 0, ctx.target_id);
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        if (ctx.phase == SAFETY_PHASE_RECOVERED)
        {
            if (!ctx.recovered_ui_synced &&
                app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED, 0, ctx.target_id))
            {
                app_safety_mark_recovered_ui_synced();
            }
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        if (!ctx.ai_monitor_enabled ||
            (ctx.phase != SAFETY_PHASE_WINDOW_OPEN &&
             ctx.phase != SAFETY_PHASE_LOST_COUNTDOWN))
        {
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        const uint32_t now_ms = app_safety_now_ms();
        app_drone_ai_stats_t monitor_ai = {0};
        app_drone_ai_get_stats(&monitor_ai);
        const uint32_t seen_ms = monitor_ai.last_drone_seen_ms;
        const bool seen_after_monitor = seen_ms != 0U &&
                                        (int32_t)(seen_ms - ctx.monitor_start_ms) >= 0;

        if (ctx.phase == SAFETY_PHASE_WINDOW_OPEN && seen_after_monitor && !ctx.presence_seen)
        {
            taskENTER_CRITICAL(&s_safety_mux);
            s_safety.presence_seen = true;
            taskEXIT_CRITICAL(&s_safety_mux);
            ctx.presence_seen = true;
        }

        const bool no_drone_after_first_inference =
            !ctx.presence_seen && monitor_ai.inferred > 0U;
        const bool drone_left_after_seen =
            ctx.presence_seen &&
            seen_after_monitor &&
            (uint32_t)(now_ms - seen_ms) >= SAFETY_LOST_AFTER_MS;
        const bool should_start_countdown =
            ctx.phase == SAFETY_PHASE_WINDOW_OPEN &&
            !ctx.loss_countdown_consumed &&
            (no_drone_after_first_inference || drone_left_after_seen);

        if (should_start_countdown)
        {
            const TickType_t deadline = now + app_safety_ms_to_ticks(SAFETY_LOST_COUNTDOWN_MS);
            taskENTER_CRITICAL(&s_safety_mux);
            s_safety.phase = SAFETY_PHASE_LOST_COUNTDOWN;
            s_safety.countdown_deadline_tick = deadline;
            s_safety.countdown_start_ms = now_ms;
            s_safety.loss_countdown_consumed = true;
            s_safety.last_countdown_s = -1;
            const uint16_t target_id = s_safety.target_id;
            taskEXIT_CRITICAL(&s_safety_mux);
            app_drone_ai_reset_gate();
            app_drone_ai_set_continuous(true);
            if (app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_LOST, 10, target_id))
            {
                taskENTER_CRITICAL(&s_safety_mux);
                if (s_safety.phase == SAFETY_PHASE_LOST_COUNTDOWN)
                {
                    s_safety.last_countdown_s = 10;
                }
                taskEXIT_CRITICAL(&s_safety_mux);
            }
            vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
            continue;
        }

        if (ctx.phase == SAFETY_PHASE_LOST_COUNTDOWN)
        {
            const bool return_detection_armed =
                (uint32_t)(now_ms - ctx.countdown_start_ms) >= SAFETY_RETURN_ARM_MS;
            const bool seen_during_countdown =
                return_detection_armed &&
                seen_ms != 0U &&
                (int32_t)(seen_ms - (ctx.countdown_start_ms + SAFETY_RETURN_ARM_MS)) >= 0;
            if (app_drone_ai_is_drone_confirmed() || seen_during_countdown)
            {
                taskENTER_CRITICAL(&s_safety_mux);
                const uint16_t target_id = app_safety_mark_recovered_locked();
                taskEXIT_CRITICAL(&s_safety_mux);
                app_drone_ai_set_continuous(false);
                if (app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED, 0, target_id))
                {
                    app_safety_mark_recovered_ui_synced();
                }
                vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
                continue;
            }

            int32_t remain_s = app_safety_remaining_seconds(now, ctx.countdown_deadline_tick);
            if (remain_s <= 2)
            {
                taskENTER_CRITICAL(&s_safety_mux);
                const uint16_t target_id = app_safety_mark_recovered_locked();
                taskEXIT_CRITICAL(&s_safety_mux);
                app_drone_ai_set_continuous(false);
                if (app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED, 0, target_id))
                {
                    app_safety_mark_recovered_ui_synced();
                }
                vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
                continue;
            }
            if (remain_s != ctx.last_countdown_s)
            {
                taskENTER_CRITICAL(&s_safety_mux);
                const uint16_t target_id = s_safety.target_id;
                taskEXIT_CRITICAL(&s_safety_mux);
                if (app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_LOST, remain_s, target_id))
                {
                    taskENTER_CRITICAL(&s_safety_mux);
                    if (s_safety.phase == SAFETY_PHASE_LOST_COUNTDOWN)
                    {
                        s_safety.last_countdown_s = remain_s;
                    }
                    taskEXIT_CRITICAL(&s_safety_mux);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_POLL_MS));
    }
}

static bool app_safety_start_task_once(void)
{
    if (s_safety_task != NULL)
    {
        return true;
    }
    BaseType_t ok = xTaskCreate(app_safety_task,
        "safety_takeover",
        SAFETY_TASK_STACK,
        NULL,
        SAFETY_TASK_PRIO,
        &s_safety_task);
    if (ok != pdPASS)
    {
        s_safety_task = NULL;
        ESP_LOGE(TAG, "create safety task failed");
        return false;
    }
    return true;
}

static void app_safety_on_task_event(app_task_event_t event,
    const app_task_snapshot_t *snap,
    void *user_ctx)
{
    (void)user_ctx;
    if (event != APP_TASK_EVENT_STATE_CHANGED || snap == NULL)
    {
        return;
    }

    app_safety_takeover_ctx_t ctx = {0};
    app_safety_copy_ctx(&ctx);
    if (!ctx.active)
    {
        return;
    }
    if (!app_safety_task_is_owned(snap))
    {
        (void)app_safety_force_idle();
        app_drone_ai_set_continuous(false);
        app_ui_safety_takeover_set_visible(false);
        return;
    }

    switch (snap->state) {
    case APP_TASK_STATE_WAIT_APPROACH:
        if (ctx.phase == SAFETY_PHASE_STARTING)
        {
            app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_WAIT_DRONE, 0, ctx.target_id);
        }
        break;
    case APP_TASK_STATE_AUTH_PASSED:
    case APP_TASK_STATE_DOCKING:
        if (ctx.phase == SAFETY_PHASE_STARTING ||
            ctx.phase == SAFETY_PHASE_DRONE_CONFIRMED)
        {
            taskENTER_CRITICAL(&s_safety_mux);
            if (s_safety.phase == SAFETY_PHASE_STARTING ||
                s_safety.phase == SAFETY_PHASE_DRONE_CONFIRMED)
            {
                s_safety.phase = SAFETY_PHASE_WINDOW_OPEN;
            }
            taskEXIT_CRITICAL(&s_safety_mux);
        }
        if (ctx.phase == SAFETY_PHASE_STARTING ||
            ctx.phase == SAFETY_PHASE_DRONE_CONFIRMED ||
            ctx.phase == SAFETY_PHASE_WINDOW_OPEN)
        {
            app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_AUTH_PASSED, 0, ctx.target_id);
        }
        break;
    case APP_TASK_STATE_COMPLETED:
        if (ctx.phase == SAFETY_PHASE_TYPHOON ||
            ctx.phase == SAFETY_PHASE_SAFE_DONE)
        {
            taskENTER_CRITICAL(&s_safety_mux);
            s_safety.phase = SAFETY_PHASE_SAFE_DONE;
            s_safety.ai_monitor_enabled = false;
            s_safety.countdown_deadline_tick = 0;
            s_safety.countdown_start_ms = 0;
            s_safety.last_countdown_s = -1;
            s_safety.done_tick = xTaskGetTickCount();
            taskEXIT_CRITICAL(&s_safety_mux);
            app_drone_ai_set_continuous(false);
        }
        else
        {
            taskENTER_CRITICAL(&s_safety_mux);
            const uint16_t target_id = app_safety_mark_recovered_locked();
            taskEXIT_CRITICAL(&s_safety_mux);
            app_drone_ai_set_continuous(false);
            app_ui_safety_takeover_set_visible(true);
            if (app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED, 0, target_id))
            {
                app_safety_mark_recovered_ui_synced();
            }
        }
        break;
    case APP_TASK_STATE_FAULT:
    case APP_TASK_STATE_CANCELLED:
        if (ctx.phase != SAFETY_PHASE_TYPHOON &&
            ctx.phase != SAFETY_PHASE_SAFE_DONE &&
            ctx.phase != SAFETY_PHASE_FAILED)
        {
            taskENTER_CRITICAL(&s_safety_mux);
            s_safety.phase = SAFETY_PHASE_FAILED;
            s_safety.ai_monitor_enabled = false;
            s_safety.done_tick = xTaskGetTickCount();
            taskEXIT_CRITICAL(&s_safety_mux);
            app_drone_ai_set_continuous(false);
            app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_FAILED, 0, ctx.target_id);
        }
        break;
    default:
        break;
    }
}

static void app_safety_register_task_callback_once(void)
{
    if (s_task_callback_registered)
    {
        return;
    }
    esp_err_t ret = app_task_register_event_callback(app_safety_on_task_event, NULL);
    if (ret == ESP_OK)
    {
        s_task_callback_registered = true;
    }
    else
    {
        ESP_LOGW(TAG, "register safety task callback failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t app_safety_takeover_start(void)
{
    app_safety_takeover_ctx_t current = {0};
    app_safety_copy_ctx(&current);
    if (current.active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    app_task_snapshot_t snap = {0};
    if (!app_task_peek_snapshot(&snap))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (snap.active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint16_t target_id = snap.target_id;
    if (!app_safety_start_task_once())
    {
        return ESP_ERR_NO_MEM;
    }
    app_safety_register_task_callback_once();

    taskENTER_CRITICAL(&s_safety_mux);
    if (s_safety.active)
    {
        taskEXIT_CRITICAL(&s_safety_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_safety.active = true;
    s_safety.ai_monitor_enabled = false;
    s_safety.typhoon_task_running = false;
    s_safety.target_id = target_id;
    s_safety.task_generation = 0U;
    s_safety.phase = SAFETY_PHASE_STARTING;
    s_safety.countdown_deadline_tick = 0;
    s_safety.done_tick = 0;
    s_safety.monitor_start_ms = 0;
    s_safety.countdown_start_ms = 0;
    s_safety.presence_seen = false;
    s_safety.loss_countdown_consumed = false;
    s_safety.recovered_ui_synced = false;
    s_safety.last_countdown_s = -1;
    taskEXIT_CRITICAL(&s_safety_mux);

    app_drone_ai_set_continuous(false);
    app_ui_safety_takeover_set_visible(true);
    app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_STARTING, 0, target_id);
    app_drone_ai_reset_gate();

    esp_err_t ret = app_task_start_with_target(target_id, "safety");
    if (ret != ESP_OK)
    {
        (void)app_safety_force_idle();
        app_drone_ai_set_continuous(false);
        app_ui_safety_takeover_set_visible(false);
        return ret;
    }
    app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_WAIT_DRONE, 0, target_id);
    return ESP_OK;
}

esp_err_t app_safety_takeover_trigger_typhoon(void)
{
    app_safety_takeover_ctx_t ctx = {0};
    app_safety_copy_ctx(&ctx);
    if (!ctx.active)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!app_safety_current_task_is_owned(true))
    {
        (void)app_safety_force_idle();
        app_drone_ai_set_continuous(false);
        app_ui_safety_takeover_set_visible(false);
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_safety_mux);
    if (!s_safety.active)
    {
        taskEXIT_CRITICAL(&s_safety_mux);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_safety.typhoon_task_running)
    {
        taskEXIT_CRITICAL(&s_safety_mux);
        return ESP_OK;
    }
    s_safety.phase = SAFETY_PHASE_TYPHOON;
    s_safety.ai_monitor_enabled = false;
    s_safety.countdown_deadline_tick = 0;
    s_safety.typhoon_task_running = true;
    const uint16_t target_id = s_safety.target_id;
    taskEXIT_CRITICAL(&s_safety_mux);

    app_drone_ai_set_continuous(false);
    app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_TYPHOON, 0, target_id);
    BaseType_t ok = xTaskCreate(app_safety_typhoon_task,
        "safety_typhoon",
        SAFETY_TASK_STACK,
        NULL,
        SAFETY_TASK_PRIO,
        NULL);
    if (ok != pdPASS)
    {
        taskENTER_CRITICAL(&s_safety_mux);
        s_safety.typhoon_task_running = false;
        s_safety.phase = SAFETY_PHASE_FAILED;
        s_safety.done_tick = xTaskGetTickCount();
        taskEXIT_CRITICAL(&s_safety_mux);
        app_drone_ai_set_continuous(false);
        app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_FAILED, 0, target_id);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_safety_takeover_mark_failed(void)
{
    app_safety_takeover_ctx_t ctx = {0};
    app_safety_copy_ctx(&ctx);
    if (!ctx.active)
    {
        return;
    }
    if (ctx.phase == SAFETY_PHASE_SAFE_DONE)
    {
        return;
    }

    taskENTER_CRITICAL(&s_safety_mux);
    s_safety.phase = SAFETY_PHASE_FAILED;
    s_safety.ai_monitor_enabled = false;
    s_safety.countdown_deadline_tick = 0;
    s_safety.countdown_start_ms = 0;
    s_safety.last_countdown_s = -1;
    s_safety.done_tick = xTaskGetTickCount();
    const uint16_t target_id = s_safety.target_id;
    taskEXIT_CRITICAL(&s_safety_mux);

    app_drone_ai_set_continuous(false);
    app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_FAILED, 0, target_id);
}

void app_safety_takeover_on_ch32_line(const app_ch32_line_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    app_safety_takeover_ctx_t ctx = {0};
    app_safety_copy_ctx(&ctx);
    if (!ctx.active)
    {
        return;
    }
    const bool require_active_owner =
        ctx.phase != SAFETY_PHASE_TYPHOON &&
        ctx.phase != SAFETY_PHASE_SAFE_DONE;
    app_task_snapshot_t owner = {0};
    const bool have_owner = app_task_peek_snapshot(&owner) &&
                            app_safety_task_is_owned(&owner) &&
                            (!require_active_owner || owner.active);
    if (!have_owner)
    {
        if (ctx.phase == SAFETY_PHASE_STARTING && ctx.task_generation == 0U)
        {
            return;
        }
        (void)app_safety_force_idle();
        app_drone_ai_set_continuous(false);
        app_ui_safety_takeover_set_visible(false);
        return;
    }

    if (app_safety_stage_safe_done(msg) &&
        (ctx.phase == SAFETY_PHASE_TYPHOON || ctx.phase == SAFETY_PHASE_SAFE_DONE))
    {
        taskENTER_CRITICAL(&s_safety_mux);
        s_safety.phase = SAFETY_PHASE_SAFE_DONE;
        s_safety.ai_monitor_enabled = false;
        s_safety.done_tick = xTaskGetTickCount();
        taskEXIT_CRITICAL(&s_safety_mux);
        app_drone_ai_set_continuous(false);
        (void)app_task_mark_completed_if_current(&owner, "safety takeover safe done");
        return;
    }

    if (!app_safety_stage_tray_out(msg) ||
        ctx.phase == SAFETY_PHASE_TYPHOON ||
        ctx.phase == SAFETY_PHASE_SAFE_DONE ||
        ctx.phase == SAFETY_PHASE_FAILED ||
        ctx.phase == SAFETY_PHASE_RECOVERED ||
        ctx.loss_countdown_consumed ||
        ctx.ai_monitor_enabled)
    {
        return;
    }

    taskENTER_CRITICAL(&s_safety_mux);
    app_safety_mark_window_open_locked();
    const uint16_t target_id = s_safety.target_id;
    taskEXIT_CRITICAL(&s_safety_mux);

    app_drone_ai_reset_gate();
    app_drone_ai_set_continuous(true);
    app_safety_set_ui(APP_UI_SAFETY_TAKEOVER_WINDOW_OPEN, 0, target_id);
}

bool app_safety_takeover_ai_monitor_enabled(void)
{
    bool enabled = false;
    taskENTER_CRITICAL(&s_safety_mux);
    enabled = s_safety.active && s_safety.ai_monitor_enabled;
    taskEXIT_CRITICAL(&s_safety_mux);
    return enabled && app_safety_current_task_is_owned(true);
}

bool app_safety_takeover_preview_active(void)
{
    bool active = false;
    taskENTER_CRITICAL(&s_safety_mux);
    active = s_safety.active &&
             s_safety.phase != SAFETY_PHASE_SAFE_DONE &&
             s_safety.phase != SAFETY_PHASE_FAILED;
    taskEXIT_CRITICAL(&s_safety_mux);
    return active && app_safety_current_task_is_owned(false);
}

bool app_safety_takeover_is_active(void)
{
    bool active = false;
    taskENTER_CRITICAL(&s_safety_mux);
    active = s_safety.active;
    taskEXIT_CRITICAL(&s_safety_mux);
    return active;
}
