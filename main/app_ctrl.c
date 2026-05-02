/*
 * app_ctrl.c - 接驳主控制状态机模块（详细注释版）
 *
 * 这个文件是 SkyAnchor 的“业务大脑”之一，负责把视觉判定、任务状态、CH32 执行反馈串成完整流程。
 * 典型链路是：
 * 1. app_dock_judge 判断无人机已经对准且允许接驳；
 * 2. app_ctrl 下发 START_DOCK / OPEN / EXTEND 等命令给 CH32；
 * 3. CH32 执行舱门、托盘、称重、回收、上锁，并不断回传状态；
 * 4. app_ctrl 根据 CH32 状态更新 UI、任务状态和云端快照；
 * 5. 异常时进入冷却、保护或等待人工处理。
 *
 * 这个模块最需要注意的是“防重复触发”：识别结果每帧都会更新，如果不加冷却和状态门控，
 * 可能会反复给 CH32 下发开舱命令。因此文件里有 retrigger cooldown、busy deadline、cargo wait window 等保护逻辑。
 */

#include "app_ctrl.h"                              // 项目自定义模块头文件，声明 app_ctrl 对外提供的接口。
#include <inttypes.h>                              // 跨平台整数格式化宏，方便日志打印固定宽度整数。
#include <stdbool.h>                               // C99 布尔类型支持，提供 bool、true、false。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、strlcpy。
#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include "esp_err.h"                               // ESP-IDF 错误码类型 esp_err_t 和 ESP_OK 等定义。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "app_ch32_link.h"                         // 项目自定义模块头文件，声明 app_ch32_link 对外提供的接口。
#include "app_dock_judge.h"                        // 项目自定义模块头文件，声明 app_dock_judge 对外提供的接口。
#include "app_task.h"                              // 项目自定义模块头文件，声明 app_task 对外提供的接口。
#include "app_ui.h"                                // 项目自定义模块头文件，声明 app_ui 对外提供的接口。
#include "app_vision.h"                            // 项目自定义模块头文件，声明 app_vision 对外提供的接口。
static const char *TAG = "app_ctrl";                             // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
#define CTRL_TASK_STACK_SIZE            (7 * 1024)       // FreeRTOS 任务栈大小，单位一般是字节。
#define CTRL_TASK_PRIORITY              5                // FreeRTOS 任务优先级，数值越大优先级越高。
#define CTRL_TASK_CORE_ID               1
#define CTRL_POLL_MS                    60U
#define CTRL_READY_PROBE_INTERVAL_MS    1000U            // 周期/间隔参数，用于控制采样或刷新频率。
#define CTRL_DOCK_CMD                   ('A')            // 命令码或命令字符串相关定义。
#define CTRL_ACK_WAIT_MS                2000U
#define CTRL_BUSY_TIMEOUT_MS            20000U           // 超时时间，避免外设或通信异常时一直阻塞。
#define CTRL_NOTICE_SHOW_MS             1600U
#define CTRL_RETRIGGER_COOLDOWN_MS      1800U
#define CTRL_AUTO_DOCK_ENABLE           (1)
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    bool inited;
    bool ch32_ready;
    bool dock_busy;
    bool cargo_wait_window_seen;
    bool has_weight;
    int32_t last_weight_g;
    uint16_t last_proto_flags;
    app_ch32_proto_stage_t last_proto_stage;
    uint8_t last_proto_error;
    uint16_t applied_target_id;
    uint32_t last_ready_probe_ms;
    uint32_t busy_deadline_ms;
    uint32_t notice_deadline_ms;
    uint32_t retrigger_deadline_ms;
    char notice[96];
} app_ctrl_runtime_t;
static TaskHandle_t s_ctrl_task = NULL;                          // 模块级静态变量 s_ctrl_task，只在本文件内部使用，避免被其他文件直接修改。
static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;   // 模块级静态变量 s_ctrl_mux，只在本文件内部使用，避免被其他文件直接修改。
static app_ctrl_runtime_t s_rt = {0};                            // 模块级静态变量 s_rt，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 把 FreeRTOS tick 转成毫秒时间，用于超时和冷却判断。
 */
static inline uint32_t app_ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
/*
 * 判断某个截止时间是否仍然有效，处理 tick 溢出时仍保持正确。
 */
static inline bool app_ctrl_deadline_active(uint32_t deadline_ms, uint32_t now_ms)
{
    return (deadline_ms != 0U) && ((int32_t)(deadline_ms - now_ms) > 0);
}
/*
 * 在已经持有状态锁时设置 UI/状态提示文本和保持时间。
 */
static void app_ctrl_set_notice_locked(const char *text, uint32_t hold_ms)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (text == NULL) {
        s_rt.notice[0] = '\0';
        s_rt.notice_deadline_ms = 0;
        return;
    }
    strlcpy(s_rt.notice, text, sizeof(s_rt.notice));
    s_rt.notice_deadline_ms = app_ctrl_now_ms() + hold_ms;
}
/*
 * 线程安全地设置控制提示文本。
 */
static void app_ctrl_set_notice(const char *text, uint32_t hold_ms)
{
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_ctrl_mux);
    app_ctrl_set_notice_locked(text, hold_ms);
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_ctrl_mux);
}
/*
 * 启动防重复触发冷却时间，避免一直接收到 ready 视觉结果时反复下发接驳命令。
 */
static void app_ctrl_start_retrigger_cooldown_locked(uint32_t hold_ms)
{
    s_rt.retrigger_deadline_ms = app_ctrl_now_ms() + hold_ms;
}
/*
 * 判断新版协议阶段是否代表 CH32 正在执行动作。
 */
static bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage)
{
    switch (stage) {
        case APP_CH32_STAGE_DOOR_OPENING:
        case APP_CH32_STAGE_DOOR_OPENED:
        case APP_CH32_STAGE_TRAY_EXTENDING:
        case APP_CH32_STAGE_TRAY_EXTENDED:
        case APP_CH32_STAGE_WAITING_CARGO:
        case APP_CH32_STAGE_CARGO_DETECTED:
        case APP_CH32_STAGE_TRAY_RETRACTING:
        case APP_CH32_STAGE_TRAY_RETRACTED:
        case APP_CH32_STAGE_DOOR_CLOSING:
            return true;
        default:
            return false;
    }
}
/*
 * 判断新版协议阶段是否处于等待货物放入托盘的窗口。
 */
static bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage)
{
    return (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
           (stage == APP_CH32_STAGE_WAITING_CARGO);
}
/*
 * 根据 flags 判断托盘是否已经伸出。
 */
static bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags)
{
    return (flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U;
}
/*
 * 判断错误是否属于等待货物阶段的软错误，避免过早进入硬故障。
 */
static bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error)
{
    return (proto_error == APP_CH32_ERR_TIMEOUT) ||
           (proto_error == APP_CH32_ERR_WEIGHT);
}
/*
 * 把 CH32 阶段转换为适合显示在 UI 上的状态文本。
 */
static const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage)
{
    switch (stage) {
        case APP_CH32_STAGE_IDLE:            return "dock: CH32 idle";
        case APP_CH32_STAGE_READY:           return "dock: CH32 ready";
        case APP_CH32_STAGE_DOOR_OPENING:    return "dock: door opening";
        case APP_CH32_STAGE_DOOR_OPENED:     return "dock: door opened";
        case APP_CH32_STAGE_TRAY_EXTENDING:  return "dock: tray extending";
        case APP_CH32_STAGE_TRAY_EXTENDED:   return "dock: tray extended";
        case APP_CH32_STAGE_WAITING_CARGO:   return "dock: waiting cargo";
        case APP_CH32_STAGE_CARGO_DETECTED:  return "dock: cargo detected";
        case APP_CH32_STAGE_TRAY_RETRACTING: return "dock: tray retracting";
        case APP_CH32_STAGE_TRAY_RETRACTED:  return "dock: tray retracted";
        case APP_CH32_STAGE_DOOR_CLOSING:    return "dock: door closing";
        case APP_CH32_STAGE_SAFE_LOCKED:     return "dock: safe locked";
        case APP_CH32_STAGE_COMPLETE:        return "dock: cycle complete";
        case APP_CH32_STAGE_FAULT:           return "dock: CH32 fault";
        case APP_CH32_STAGE_UNKNOWN:
        default:                             return "dock: CH32 online";
    }
}
/*
 * 判断该阶段是否需要 busy 超时保护。
 */
static bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage)
{
    return !app_ctrl_proto_stage_is_cargo_wait_window(stage);
}
/*
 * 判断当前错误是否可以按等待货物阶段处理，防止投递等待时误判成严重故障。
 */
static bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
                                                 uint16_t prev_flags,
                                                 app_ch32_proto_stage_t stage,
                                                 uint16_t flags,
                                                 uint8_t proto_error,
                                                 bool cargo_wait_window_seen)
{
    const bool tray_out_now = app_ctrl_proto_flags_indicate_tray_out(flags);
    const bool tray_out_before = app_ctrl_proto_flags_indicate_tray_out(prev_flags);
    if (!app_ctrl_proto_error_is_cargo_wait_soft(proto_error)) {
        return false;
    }
    if ((stage == APP_CH32_STAGE_FAULT) ||
        (stage == APP_CH32_STAGE_SAFE_LOCKED) ||
        (stage == APP_CH32_STAGE_COMPLETE) ||
        ((flags & APP_CH32_FLAG_LOCKED) != 0U)) {
        return false;
    }
    if (cargo_wait_window_seen) {
        return app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
               tray_out_now ||
               app_ctrl_proto_stage_is_cargo_wait_window(prev_stage) ||
               tray_out_before;
    }
    if (app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage)) {
        return true;
    }
    return tray_out_now &&
           ((stage == APP_CH32_STAGE_TRAY_EXTENDING) ||
            (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
            (stage == APP_CH32_STAGE_WAITING_CARGO));
}
/*
 * 进入或延长等待货物状态，给无人机投放货物留出时间。
 */
static void app_ctrl_hold_waiting_cargo_locked(void)
{
    s_rt.cargo_wait_window_seen = true;
    s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.dock_busy = true;
    s_rt.busy_deadline_ms = 0;
    app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
}
/*
 * 组合接驳调试详情文本，给 UI 的 dock dbg 区域显示。
 */
static void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
                                    bool has_weight,
                                    int32_t weight_g,
                                    app_ch32_proto_stage_t proto_stage,
                                    char *buf,
                                    size_t buf_len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U)) {
        return;
    }

    /*
     * 视觉无效时，调试区优先显示“等待有效 tag”或“保持上一帧”的信息。
     *
     * hold 状态对现场调试很重要：如果屏幕短暂保留识别框，
     * 你需要知道这是新识别结果，还是 lost_hold_frames 带来的短暂保持。
     */
    if (!dock->vision_valid) {
        if (dock->state != APP_DOCK_STATE_SEARCHING) {
            snprintf(buf,
                     buf_len,
                     "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ldmm e:%.1f stage:%s",
                     (unsigned)dock->invalid_hold_count,
                     (unsigned)dock->lost_count,
                     (long)dock->dx,
                     (long)dock->dy,
                     (long)dock->est_distance_mm,
                     (double)dock->filtered_edge_px,
                     app_ch32_link_proto_stage_name(proto_stage));
        } else {
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        }
        return;
    }

    /*
     * 视觉有效时，把关键工程量压缩成一行：
     * id / dx / dy / z / edge / angle / stable / score / weight。
     *
     * 这些字段基本覆盖了“为什么还没接驳”的主要原因。
     */
    snprintf(buf,
             buf_len,
             "dock dbg: id:%u dx:%ld dy:%ld z:%ldmm e:%.1f ang:%d st:%u score:%u wt:%s%ldg",
             (unsigned)dock->tag_id,
             (long)dock->dx,
             (long)dock->dy,
             (long)dock->est_distance_mm,
             (double)dock->filtered_edge_px,
             (int)dock->angle_deg,
             (unsigned)dock->stable_count,
             (unsigned)dock->hover_score,
             has_weight ? "" : "-",
             has_weight ? (long)weight_g : 0L);
}
/*
 * 根据接驳判定结果生成引导提示，例如向左/向右/靠近/远离。
 */
static void app_ctrl_compose_guidance(const app_dock_judge_result_t *dock,
                                      char *buf,
                                      size_t buf_len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U)) {
        return;
    }

    /*
     * 按“最先阻塞接驳的原因”生成提示。
     *
     * 这个顺序是有意设计的：
     * 先确认是否看到目标，再确认 ID，再确认中心、距离、稳定性。
     * 用户看到提示时可以按这个顺序调整无人机。
     */
    if (!dock->vision_valid) {
        snprintf(buf, buf_len, "dock: searching target");
        return;
    }
    if (!dock->target_id_ok) {
        snprintf(buf, buf_len, "dock: wrong tag id");
        return;
    }
    if (!dock->centered_ok) {
        snprintf(buf, buf_len, "dock: align target center");
        return;
    }
    if (!dock->near_ok) {
        snprintf(buf, buf_len, "dock: move target closer");
        return;
    }
    if (!dock->stable_ok) {
        snprintf(buf, buf_len, "dock: hold hover stable");
        return;
    }
    if (!dock->distance_ok) {
        /*
         * 距离估算有效时区分 too near / too far；
         * 距离估算无效时提示等待有效距离，而不是给出错误方向。
         */
        if (dock->est_distance_mm > 0) {
            snprintf(buf,
                     buf_len,
                     (dock->est_distance_mm < 260) ? "dock: target too near" : "dock: target too far");
        } else {
            snprintf(buf, buf_len, "dock: wait valid distance");
        }
        return;
    }
    app_dock_judge_format_status(dock, buf, buf_len);
}
/*
 * 组合任务状态文本，把任务层状态和接驳层状态合并显示。
 */
static void app_ctrl_compose_task_status(const app_task_snapshot_t *task,
                                         const app_dock_judge_result_t *dock,
                                         bool ch32_ready,
                                         char *buf,
                                         size_t buf_len)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (buf == NULL || buf_len == 0U || task == NULL) {
        return;
    }

    /*
     * 任务层状态是“业务流程”，dock 判定是“视觉/安全条件”。
     * 这里把两者合并成一行给主状态栏显示，让屏幕上既能看到任务阶段，也能看到当前阻塞原因。
     */
    switch (task->state) {
        case APP_TASK_STATE_CONFIGURED:
            snprintf(buf,
                     buf_len,
                     ch32_ready ? "task: target=%u / remote ready" : "task: target=%u / wait CH32",
                     (unsigned)task->target_id);
            break;
        case APP_TASK_STATE_WAIT_APPROACH: {
            /*
             * 等待无人机靠近时，附带接驳引导提示。
             */
            char guide[72] = {0};
            app_ctrl_compose_guidance(dock, guide, sizeof(guide));
            snprintf(buf, buf_len, "task: wait id=%u / %s", (unsigned)task->target_id, guide);
            break;
        }
        case APP_TASK_STATE_AUTH_PASSED:
            snprintf(buf,
                     buf_len,
                     "task: auth passed / matched id=%u",
                     (unsigned)(task->matched_tag_id != 0U ? task->matched_tag_id : task->target_id));
            break;
        case APP_TASK_STATE_DOCKING:
            snprintf(buf, buf_len, "task: docking in progress");
            break;
        case APP_TASK_STATE_COMPLETED:
            snprintf(buf,
                     buf_len,
                     "task: completed / target=%u",
                     (unsigned)task->target_id);
            break;
        case APP_TASK_STATE_FAULT:
            snprintf(buf,
                     buf_len,
                     "task: fault / %s",
                     task->note[0] != '\0' ? task->note : "check CH32");
            break;
        case APP_TASK_STATE_CANCELLED:
            snprintf(buf, buf_len, "task: cancelled / target=%u", (unsigned)task->target_id);
            break;
        case APP_TASK_STATE_IDLE:
        default:
            snprintf(buf, buf_len, "task: idle");
            break;
    }
}
/*
 * 在持锁状态下把 CH32 新版协议消息合并进主控状态机。
 */
static void app_ctrl_apply_proto_msg_locked(const app_ch32_line_t *msg)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (msg == NULL) {
        return;
    }

    /*
     * 保存上一帧 CH32 状态。
     *
     * 某些 CH32 错误不是硬故障，例如已经进入等待货物窗口后短暂检测不到货物。
     * 这类“软等待”要结合前一阶段和 flags 判断，不能只看当前错误码。
     */
    const app_ch32_proto_stage_t prev_proto_stage = s_rt.last_proto_stage;
    const uint16_t prev_proto_flags = s_rt.last_proto_flags;
    const bool prev_cargo_wait_window_seen = s_rt.cargo_wait_window_seen;

    /*
     * 同步 CH32 ready 状态。
     * app_ch32_link.c 会在收到 READY flag 或 ready 阶段时更新内部 ready 标志。
     */
    s_rt.ch32_ready = app_ch32_link_is_ready();

    /*
     * 标准状态 payload 至少 8 字节，包含 flags 和 weight。
     * 只有长度足够时才更新重量，避免短 ACK/NACK 把旧重量覆盖掉。
     */
    if (msg->payload_len >= 8U) {
        s_rt.last_weight_g = msg->proto_weight_g;
        s_rt.has_weight = true;
        s_rt.last_proto_flags = msg->proto_flags;
    }

    /*
     * 只有状态类协议消息才会影响机械流程状态机。
     * ACK/NACK 主要由 app_ch32_link_send_cmd_and_wait_ack() 消费。
     */
    if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
        (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
        (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
        (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        if (msg->payload_len >= 8U) {
            s_rt.last_proto_flags = msg->proto_flags;
        }

        /*
         * cargo_wait_window_seen 表示 CH32 已经走到“等待货物/托盘已伸出”附近。
         * 后续如果出现货物相关软错误，可以据此判断是继续等待还是进入故障。
         */
        if (app_ctrl_proto_stage_is_cargo_wait_window(msg->proto_stage) ||
            ((msg->payload_len >= 8U) && app_ctrl_proto_flags_indicate_tray_out(msg->proto_flags))) {
            s_rt.cargo_wait_window_seen = true;
        }

        /*
         * 阶段变化时更新提示文案。
         */
        if (s_rt.last_proto_stage != msg->proto_stage) {
            s_rt.last_proto_stage = msg->proto_stage;
            app_ctrl_set_notice_locked(app_ctrl_proto_stage_status_text(msg->proto_stage), CTRL_NOTICE_SHOW_MS);
        }

        /*
         * 错误处理。
         *
         * 先判断是否属于“等待货物窗口内的软等待错误”；
         * 如果不是软等待，就记录错误、退出 busy，并启动防重复触发冷却。
         */
        if ((msg->type == APP_CH32_LINE_PROTO_ERROR) || (msg->proto_stage == APP_CH32_STAGE_FAULT)) {
            if (app_ctrl_is_soft_waiting_cargo_error(prev_proto_stage,
                                                     prev_proto_flags,
                                                     msg->proto_stage,
                                                     msg->proto_flags,
                                                     msg->proto_detail,
                                                     prev_cargo_wait_window_seen)) {
                app_ctrl_hold_waiting_cargo_locked();
                return;
            }
            s_rt.last_proto_error = msg->proto_detail;
            s_rt.dock_busy = false;
            s_rt.cargo_wait_window_seen = false;
            s_rt.busy_deadline_ms = 0;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
            snprintf(s_rt.notice,
                     sizeof(s_rt.notice),
                     "dock: CH32 err %s",
                     app_ch32_link_proto_error_name(msg->proto_detail));
            s_rt.notice_deadline_ms = app_ctrl_now_ms() + CTRL_NOTICE_SHOW_MS;
            return;
        }

        /*
         * busy 状态处理。
         *
         * CH32 flags 里显式 BUSY，或者 stage 属于机械动作阶段，都认为接驳流程正在执行。
         * 某些阶段需要超时保护，某些阶段（例如等待货物）不应该简单按固定时间判超时。
         */
        if (((msg->proto_flags & APP_CH32_FLAG_BUSY) != 0U) || app_ctrl_proto_stage_is_busy(msg->proto_stage)) {
            s_rt.dock_busy = true;
            if (app_ctrl_proto_stage_uses_busy_deadline(msg->proto_stage)) {
                s_rt.busy_deadline_ms = app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS;
            } else {
                s_rt.busy_deadline_ms = 0;
            }
            return;
        }

        /*
         * 安全结束或回到空闲状态。
         *
         * 这些阶段说明 CH32 当前不再执行危险机械动作，可以允许下一次接驳触发，
         * 但仍会加一段 retrigger cooldown，避免视觉 ready 连续帧立刻重复下发命令。
         */
        if ((msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED) ||
            (msg->proto_stage == APP_CH32_STAGE_COMPLETE) ||
            (msg->proto_stage == APP_CH32_STAGE_IDLE) ||
            (msg->proto_stage == APP_CH32_STAGE_READY)) {
            s_rt.dock_busy = false;
            s_rt.cargo_wait_window_seen = false;
            s_rt.busy_deadline_ms = 0;
            s_rt.last_proto_error = APP_CH32_ERR_NONE;
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
        }
    }
}
/*
 * CH32 通信模块收到消息后的上层回调入口，负责更新控制状态。
 */
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx)
{
    /*
     * 当前回调没有使用 user_ctx。
     * 保留参数是为了匹配 app_ch32_link_init() 的通用回调签名。
     */
    (void)user_ctx;

    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (msg == NULL) {
        return;
    }

    /*
     * CH32 回调可能来自 UART RX 任务，而控制任务也会读取 s_rt。
     * 所以修改 s_rt 前必须进入临界区。
     */
    // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
    taskENTER_CRITICAL(&s_ctrl_mux);

    app_ctrl_apply_proto_msg_locked(msg);
    // 退出临界区：共享变量访问结束，恢复正常调度/中断。
    taskEXIT_CRITICAL(&s_ctrl_mux);
}
/*
 * 控制后台任务，周期性读取视觉/接驳判定结果，并决定是否触发 CH32 接驳流程。
 */
static void app_ctrl_task(void *arg)
{
    (void)arg;
    bool prev_ready_level = false;
    bool prev_dock_busy = false;
    while (1) {
        const uint32_t now_ms = app_ctrl_now_ms();
        app_vision_result_t vision = {0};
        app_dock_judge_result_t dock = {0};
        app_task_snapshot_t task = {0};
/*
 * 读取最新视觉识别结果，供接驳判定模块使用。
 */
        (void)app_vision_get_latest_result(&vision);
/*
 * 接驳判定核心函数：根据最新视觉结果更新状态机，并输出当前是否可接驳。
 */
        (void)app_dock_judge_process(&vision, &dock);
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
        (void)app_task_get_snapshot(&task);
        bool ch32_ready = false;
        bool dock_busy = false;
        bool has_weight = false;
        int32_t weight_g = 0;
        app_ch32_proto_stage_t proto_stage = APP_CH32_STAGE_UNKNOWN;
        uint16_t proto_flags = 0;
        uint8_t proto_error = APP_CH32_ERR_NONE;
        bool cargo_wait_window_seen = false;
        uint32_t last_probe_ms = 0;
        uint32_t busy_deadline_ms = 0;
        uint32_t notice_deadline_ms = 0;
        uint32_t retrigger_deadline_ms = 0;
        char notice[96] = {0};
        uint16_t applied_target_id = 0;
        // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
        taskENTER_CRITICAL(&s_ctrl_mux);
        s_rt.ch32_ready = app_ch32_link_is_ready();
        ch32_ready = s_rt.ch32_ready;
        dock_busy = s_rt.dock_busy;
        cargo_wait_window_seen = s_rt.cargo_wait_window_seen;
        has_weight = s_rt.has_weight;
        weight_g = s_rt.last_weight_g;
        proto_stage = s_rt.last_proto_stage;
        proto_flags = s_rt.last_proto_flags;
        proto_error = s_rt.last_proto_error;
        last_probe_ms = s_rt.last_ready_probe_ms;
        busy_deadline_ms = s_rt.busy_deadline_ms;
        notice_deadline_ms = s_rt.notice_deadline_ms;
        retrigger_deadline_ms = s_rt.retrigger_deadline_ms;
        applied_target_id = s_rt.applied_target_id;
        strlcpy(notice, s_rt.notice, sizeof(notice));
        // 退出临界区：共享变量访问结束，恢复正常调度/中断。
        taskEXIT_CRITICAL(&s_ctrl_mux);
        if ((task.target_dirty) || (applied_target_id != task.target_id)) {
            if (app_dock_judge_set_target_id(task.target_id, true) == ESP_OK) {
                // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
                taskENTER_CRITICAL(&s_ctrl_mux);
                s_rt.applied_target_id = task.target_id;
                // 退出临界区：共享变量访问结束，恢复正常调度/中断。
                taskEXIT_CRITICAL(&s_ctrl_mux);
                // 信息日志：用于确认程序执行到了哪个阶段。
                ESP_LOGI(TAG, "applied target id => %u", (unsigned)task.target_id);
            }
        }
        if (!ch32_ready && (now_ms - last_probe_ms >= CTRL_READY_PROBE_INTERVAL_MS)) {
            // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
            taskENTER_CRITICAL(&s_ctrl_mux);
            s_rt.last_ready_probe_ms = now_ms;
            // 退出临界区：共享变量访问结束，恢复正常调度/中断。
            taskEXIT_CRITICAL(&s_ctrl_mux);
/*
 * 主动探测 CH32 是否在线且 ready，常用于系统启动阶段。
 */
            (void)app_ch32_link_probe_ready(200);
        }
        if (dock_busy && (busy_deadline_ms != 0U) && ((int32_t)(now_ms - busy_deadline_ms) >= 0)) {
            if (app_ctrl_proto_stage_is_cargo_wait_window(proto_stage) || cargo_wait_window_seen) {
                // 进入临界区：下面代码会访问跨任务共享变量，必须短时间关中断/加锁保护。
                taskENTER_CRITICAL(&s_ctrl_mux);
                app_ctrl_hold_waiting_cargo_locked();
                taskEXIT_CRITICAL(&s_ctrl_mux);
                dock_busy = true;
                cargo_wait_window_seen = true;
                proto_stage = APP_CH32_STAGE_WAITING_CARGO;
                proto_error = APP_CH32_ERR_NONE;
                busy_deadline_ms = 0;
            } else {
                taskENTER_CRITICAL(&s_ctrl_mux);
                s_rt.dock_busy = false;
                s_rt.busy_deadline_ms = 0;
                s_rt.last_proto_error = APP_CH32_ERR_TIMEOUT;
                app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
                app_ctrl_set_notice_locked("dock: CH32 timeout", CTRL_NOTICE_SHOW_MS);
                taskEXIT_CRITICAL(&s_ctrl_mux);
                dock_busy = false;
                proto_error = APP_CH32_ERR_TIMEOUT;
                if (task.active || task.state == APP_TASK_STATE_DOCKING) {
                    app_task_mark_fault("CH32 timeout");
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
                    (void)app_task_get_snapshot(&task);
                }
            }
        }
        if (!dock_busy &&
            app_ctrl_is_soft_waiting_cargo_error(proto_stage,
                                                 proto_flags,
                                                 proto_stage,
                                                 proto_flags,
                                                 proto_error,
                                                 cargo_wait_window_seen)) {
            taskENTER_CRITICAL(&s_ctrl_mux);
            app_ctrl_hold_waiting_cargo_locked();
            taskEXIT_CRITICAL(&s_ctrl_mux);
            dock_busy = true;
            cargo_wait_window_seen = true;
            proto_stage = APP_CH32_STAGE_WAITING_CARGO;
            proto_error = APP_CH32_ERR_NONE;
            busy_deadline_ms = 0;
        }
        const bool ready_level = (dock.state == APP_DOCK_STATE_READY_TO_DOCK);
        const bool retrigger_blocked = app_ctrl_deadline_active(retrigger_deadline_ms, now_ms);
        if (task.active && (task.state == APP_TASK_STATE_WAIT_APPROACH) && ready_level) {
            app_task_mark_auth_passed(dock.tag_id);
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
            (void)app_task_get_snapshot(&task);
            app_ctrl_set_notice("auth passed / ready to dock", CTRL_NOTICE_SHOW_MS);
        }
#if CTRL_AUTO_DOCK_ENABLE
        if (task.active && !dock_busy && !retrigger_blocked && !prev_ready_level && ready_level) {
            if (!ch32_ready) {
                app_ctrl_set_notice("dock: ready but CH32 not ready", CTRL_NOTICE_SHOW_MS);
            } else {
                // 信息日志：用于确认程序执行到了哪个阶段。
                ESP_LOGI(TAG,
                         "READY rising edge -> send CH32 cmd %c (id=%u dx=%ld dy=%ld z=%ld score=%u)",
                         CTRL_DOCK_CMD,
                         (unsigned)dock.tag_id,
                         (long)dock.dx,
                         (long)dock.dy,
                         (long)dock.est_distance_mm,
                         (unsigned)dock.hover_score);
                esp_err_t ret = app_ch32_link_send_cmd_and_wait_ack(CTRL_DOCK_CMD, CTRL_ACK_WAIT_MS);
                if (ret == ESP_OK) {
                    taskENTER_CRITICAL(&s_ctrl_mux);
                    s_rt.dock_busy = true;
                    s_rt.cargo_wait_window_seen = false;
                    s_rt.last_proto_error = APP_CH32_ERR_NONE;
                    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
                    s_rt.last_proto_flags = 0;
                    s_rt.busy_deadline_ms = now_ms + CTRL_BUSY_TIMEOUT_MS;
                    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
                    app_ctrl_set_notice_locked("dock: CH32 accepted start dock", CTRL_NOTICE_SHOW_MS);
                    taskEXIT_CRITICAL(&s_ctrl_mux);
                    dock_busy = true;
                    cargo_wait_window_seen = false;
                    app_task_mark_docking_started();
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
                    (void)app_task_get_snapshot(&task);
                } else {
                    // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
                    ESP_LOGW(TAG, "send CH32 cmd %c failed: %s", CTRL_DOCK_CMD, esp_err_to_name(ret));
                    const bool ch32_rejected = (ret == ESP_ERR_INVALID_RESPONSE);
                    taskENTER_CRITICAL(&s_ctrl_mux);
                    s_rt.last_proto_error = APP_CH32_ERR_INTERNAL;
                    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
                    taskEXIT_CRITICAL(&s_ctrl_mux);
                    app_ctrl_set_notice(ch32_rejected ? "dock: CH32 rejected cmd" : "dock: CH32 ack timeout",
                                        CTRL_NOTICE_SHOW_MS);
                    app_task_mark_fault(ch32_rejected ? "CH32 rejected cmd" : "CH32 ack timeout");
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
                    (void)app_task_get_snapshot(&task);
                }
            }
        }
#endif
        if (!prev_dock_busy && dock_busy && task.active && task.state != APP_TASK_STATE_DOCKING) {
            app_task_mark_docking_started();
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
            (void)app_task_get_snapshot(&task);
        }
        if (prev_dock_busy && !dock_busy) {
            if (proto_error == APP_CH32_ERR_NONE) {
                if (task.state == APP_TASK_STATE_DOCKING || task.active) {
                    app_task_mark_completed("dock cycle done");
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
                    (void)app_task_get_snapshot(&task);
                }
            } else {
                app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
                (void)app_task_get_snapshot(&task);
            }
        }
        if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy && (task.active || task.state == APP_TASK_STATE_DOCKING)) {
            app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));
/*
 * 读取当前任务快照，给 UI 或 MQTT 状态上报使用。
 */
            (void)app_task_get_snapshot(&task);
        }
        prev_ready_level = ready_level;
        prev_dock_busy = dock_busy;
        char status[128] = {0};
        char detail[224] = {0};
        char task_brief[96] = {0};
        app_task_format_brief(&task, task_brief, sizeof(task_brief));
        app_ctrl_compose_task_status(&task, &dock, ch32_ready, status, sizeof(status));
        app_ctrl_compose_detail(&dock, has_weight, weight_g, proto_stage, detail, sizeof(detail));
        if (dock_busy) {
            snprintf(status, sizeof(status), "%s", app_ctrl_proto_stage_status_text(proto_stage));
        }
        if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy) {
            snprintf(status,
                     sizeof(status),
                     "dock: CH32 err %s",
                     app_ch32_link_proto_error_name(proto_error));
        }
        if (retrigger_blocked && !dock_busy && (proto_error == APP_CH32_ERR_NONE) && (notice[0] == '\0')) {
            strlcpy(status, "dock: cooldown / wait next approach", sizeof(status));
        }
        if (app_ctrl_deadline_active(notice_deadline_ms, now_ms) && (notice[0] != '\0')) {
            strlcpy(status, notice, sizeof(status));
        }
        app_ui_set_status(status);
        app_ui_set_vision_text(task_brief);
        app_ui_set_dock_text(detail);
        app_ui_update_hud(&vision, &dock);
        // 任务延时让出 CPU，避免 while 循环空转占满系统。
        vTaskDelay(pdMS_TO_TICKS(CTRL_POLL_MS));
    }
}
/*
 * 初始化控制模块运行时状态和互斥资源。
 */
esp_err_t app_ctrl_init(void)
{
    if (s_rt.inited) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    taskENTER_CRITICAL(&s_ctrl_mux);
    memset(&s_rt, 0, sizeof(s_rt));
    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    s_rt.inited = true;
    taskEXIT_CRITICAL(&s_ctrl_mux);
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "ctrl init done (auto_dock=%d)", CTRL_AUTO_DOCK_ENABLE);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 创建并启动接驳控制任务。
 */
esp_err_t app_ctrl_start(void)
{
    if (!s_rt.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_ctrl_task != NULL) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    // 创建并固定 FreeRTOS 任务到指定 CPU 核，减少任务迁移带来的抖动。
    BaseType_t ret = xTaskCreatePinnedToCore(app_ctrl_task,
                                             "app_ctrl",
                                             CTRL_TASK_STACK_SIZE,
                                             NULL,
                                             CTRL_TASK_PRIORITY,
                                             &s_ctrl_task,
                                             CTRL_TASK_CORE_ID);
    if (ret != pdPASS) {
        s_ctrl_task = NULL;
        return ESP_FAIL;
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "ctrl task started");
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
