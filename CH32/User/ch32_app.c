#include "debug.h"
#include "TMC2209.h"
#include "L298N.h"
#include "HX711.h"
#include "esp32_com.h"
#include "ch32_app.h"

#include <string.h>

/*
 * CH32 application layer.
 *
 * This file owns the docking flow and publishes protocol STATUS frames that
 * match the ESP32 app_ch32_link stage/error/flag model.
 */

#define OUTER_DOOR_TEST_STEPS          40000U
#define OUTER_DOOR_SPEED_US            100U
#define PUSHROD_EXTEND_TIME_MS         10000U
#define PUSHROD_RETRACT_TIME_MS        10000U
#define WEIGHT_TRIGGER_G               20
#define WEIGHT_CHECK_INTERVAL_MS       200U
#define WEIGHT_CONFIRM_COUNT           3U
#define WEIGHT_WAIT_TIMEOUT_MS         15000U
#define READY_BROADCAST_INTERVAL_MS    500U
#define ACTION_POLL_MS                 10U
#define STEPPER_ABORT_POLL_STEPS       200U

typedef struct
{
    uint8_t proto_cmd; // 触发当前状态变化的命令。
    uint8_t proto_seq; // 与 ESP32 侧 ACK/状态匹配的序号。
} ch32_proto_ctx_t;

// CH32 业务运行时状态。
// action_ctx 在长动作期间固定保存原始命令，避免轮询到的新查询命令改变状态归属。
typedef struct
{
    uint8_t hx711_ready;             // 称重模块是否完成去皮并可用于货物检测。
    uint8_t action_busy;             // 机械流程执行期间置 1，用于拒绝冲突命令。
    uint8_t current_stage;           // 最近发布给 ESP32 的机械阶段。
    uint8_t last_error;              // 保留故障，直到收到 RESET_FAULT。
    uint8_t locked;                  // 外门和托盘是否已经回到安全锁止状态。
    int32_t last_weight_g;           // 最近一次有效称重结果。
    uint32_t idle_ms;                // 空闲状态广播计时。
    ch32_proto_ctx_t request_ctx;     // 最近收到的命令上下文。
    ch32_proto_ctx_t action_ctx;      // 当前长动作的固定上下文。
    uint8_t action_ctx_valid;         // action_ctx 是否可用于状态上报。
} ch32_app_runtime_t;

static ch32_app_runtime_t s_app = {
    0U,
    0U,
    ESP32_COMM_STAGE_IDLE,
    ESP32_COMM_ERR_NONE,
    0U,
    0,
    0U,
    {0U, 0U},
    {0U, 0U},
    0U,
};

static uint8_t ch32_app_close_to_safe_locked(uint8_t emit_safe_event);

/* ---------- 协议上下文与状态发布 ---------- */

// 从入站命令提取最小上下文；空包用于主动状态广播。
static ch32_proto_ctx_t ch32_app_ctx_from_packet(const ESP32_Comm_Packet_t *pkt)
{
    ch32_proto_ctx_t ctx = {0U, 0U};

    if(pkt != 0)
    {
        ctx.proto_cmd = pkt->proto_cmd;
        ctx.proto_seq = pkt->proto_seq;
    }

    return ctx;
}

static void ch32_app_set_request_ctx(const ESP32_Comm_Packet_t *pkt)
{
    s_app.request_ctx = ch32_app_ctx_from_packet(pkt);
}

static const ch32_proto_ctx_t *ch32_app_active_status_ctx(void)
{
    if(s_app.action_ctx_valid != 0U)
        return &s_app.action_ctx;

    return &s_app.request_ctx;
}

// flags 是 ESP32 快速判断 ready/busy/cargo 的摘要，详细阶段仍以 stage 为准。
static uint16_t ch32_app_build_status_flags(void)
{
    uint16_t flags = 0U;

    if((s_app.action_busy == 0U) && (s_app.last_error == ESP32_COMM_ERR_NONE))
        flags |= ESP32_COMM_FLAG_READY;
    if(s_app.action_busy != 0U)
        flags |= ESP32_COMM_FLAG_BUSY;
    if(s_app.locked != 0U)
        flags |= ESP32_COMM_FLAG_LOCKED;
    if(s_app.last_weight_g >= WEIGHT_TRIGGER_G)
        flags |= ESP32_COMM_FLAG_CARGO_PRESENT;

    return flags;
}

static void ch32_app_publish_status_ctx(uint8_t proto_type,
                                        const ch32_proto_ctx_t *ctx,
                                        uint8_t stage,
                                        uint8_t detail)
{
    ESP32_Comm_SendProtoState(proto_type,
                              (ctx != 0) ? ctx->proto_cmd : ESP32_COMM_PROTO_CMD_NONE,
                              (ctx != 0) ? ctx->proto_seq : 0U,
                              stage,
                              detail,
                              ch32_app_build_status_flags(),
                              s_app.last_weight_g);
}

static void ch32_app_publish_packet_status(const ESP32_Comm_Packet_t *pkt,
                                           uint8_t stage,
                                           uint8_t detail)
{
    ch32_proto_ctx_t ctx = ch32_app_ctx_from_packet(pkt);
    ch32_app_publish_status_ctx(ESP32_COMM_PROTO_TYPE_STATUS, &ctx, stage, detail);
}

static void ch32_app_publish_stage(uint8_t stage, uint8_t detail)
{
    s_app.current_stage = stage;

    if((stage == ESP32_COMM_STAGE_SAFE_LOCKED) || (stage == ESP32_COMM_STAGE_COMPLETE))
        s_app.locked = 1U;

    if((stage == ESP32_COMM_STAGE_IDLE) ||
       (stage == ESP32_COMM_STAGE_READY) ||
       (stage == ESP32_COMM_STAGE_SAFE_LOCKED) ||
       (stage == ESP32_COMM_STAGE_COMPLETE) ||
       (stage == ESP32_COMM_STAGE_FAULT))
    {
        s_app.action_busy = 0U;
    }

    ch32_app_publish_status_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                                ch32_app_active_status_ctx(),
                                stage,
                                detail);
}

// 故障状态会结束当前动作并持续保留，防止下一次空闲广播误报 ready。
static void ch32_app_publish_fault(uint8_t err_code)
{
    s_app.last_error = err_code;
    s_app.current_stage = ESP32_COMM_STAGE_FAULT;
    s_app.action_busy = 0U;

    ch32_app_publish_status_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                                ch32_app_active_status_ctx(),
                                ESP32_COMM_STAGE_FAULT,
                                err_code);
}

static void ch32_app_publish_idle_status(void)
{
    if(s_app.last_error == ESP32_COMM_ERR_NONE)
    {
        s_app.current_stage = ESP32_COMM_STAGE_READY;
        ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_STATUS,
                                  ESP32_COMM_PROTO_CMD_NONE,
                                  0U,
                                  ESP32_COMM_STAGE_READY,
                                  ESP32_COMM_ERR_NONE,
                                  ch32_app_build_status_flags(),
                                  s_app.last_weight_g);
    }
    else
    {
        s_app.current_stage = ESP32_COMM_STAGE_FAULT;
        ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_STATUS,
                                  ESP32_COMM_PROTO_CMD_NONE,
                                  0U,
                                  ESP32_COMM_STAGE_FAULT,
                                  s_app.last_error,
                                  ch32_app_build_status_flags(),
                                  s_app.last_weight_g);
    }
}

/* ---------- 动作生命周期与传感器 ---------- */

// 开始长动作时冻结请求上下文，之后的 STATUS 都关联到同一 cmd/seq。
static void ch32_app_begin_action(void)
{
    s_app.action_busy = 1U;
    s_app.locked = 0U;
    s_app.last_error = ESP32_COMM_ERR_NONE;
    s_app.action_ctx = s_app.request_ctx;
    s_app.action_ctx_valid = 1U;
}

static void ch32_app_stop_motion(void)
{
    PushRod_Stop();
    TMC2209_Disable();
}

static void ch32_app_finish_action(void)
{
    ch32_app_stop_motion();
    ESP32_Comm_FlushRx();
    s_app.action_busy = 0U;
    s_app.action_ctx_valid = 0U;
}

// 多次称重取平均，减少托盘振动造成的瞬时误判。
static int32_t ch32_app_read_weight_average(uint8_t times)
{
    int64_t sum = 0;

    if(times == 0U)
        times = 1U;

    for(uint8_t i = 0U; i < times; i++)
    {
        sum += HX711_Get_Weight();
        Delay_Ms(20);
    }

    s_app.last_weight_g = (int32_t)(sum / times);
    return s_app.last_weight_g;
}

static uint8_t ch32_app_ensure_hx711_ready(void)
{
    if(s_app.hx711_ready != 0U)
        return 1U;

    if(HX711_Tare() != 0U)
    {
        s_app.hx711_ready = 1U;
        return 1U;
    }

    return 0U;
}

/* ---------- 忙碌期间的可中断命令 ---------- */

// 长动作轮询期间仍允许 ABORT、SAFE_CLOSE 和状态查询，避免机械动作失控。
static uint8_t ch32_app_handle_runtime_abort(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    ch32_app_stop_motion();
    s_app.action_busy = 0U;
    s_app.action_ctx_valid = 0U;
    s_app.last_error = ESP32_COMM_ERR_NONE;
    s_app.locked = 0U;
    s_app.current_stage = ESP32_COMM_STAGE_IDLE;
    ch32_app_publish_packet_status(pkt, ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
    return 1U;
}

static uint8_t ch32_app_handle_runtime_safe_close(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    ch32_app_stop_motion();
    s_app.action_ctx = ch32_app_ctx_from_packet(pkt);
    s_app.action_ctx_valid = 1U;
    s_app.action_busy = 1U;
    s_app.locked = 0U;
    s_app.last_error = ESP32_COMM_ERR_NONE;

    if((ch32_app_close_to_safe_locked(1U) == 0U) &&
       (s_app.current_stage != ESP32_COMM_STAGE_IDLE))
    {
        ch32_app_publish_fault(ESP32_COMM_ERR_SAFETY);
    }

    ch32_app_finish_action();
    return 1U;
}

static void ch32_app_handle_query_status(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    ch32_app_publish_packet_status(pkt, s_app.current_stage, s_app.last_error);
}

static void ch32_app_handle_read_weight(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);

    if(ch32_app_ensure_hx711_ready() == 0U)
    {
        ch32_app_publish_fault(ESP32_COMM_ERR_SENSOR);
        return;
    }

    s_app.last_weight_g = ch32_app_read_weight_average(3U);
    ch32_app_publish_packet_status(pkt, s_app.current_stage, ESP32_COMM_ERR_NONE);
}

static uint8_t ch32_app_handle_runtime_command(const ESP32_Comm_Packet_t *pkt)
{
    switch(pkt->proto_cmd)
    {
        case ESP32_COMM_PROTO_CMD_ABORT:
            return ch32_app_handle_runtime_abort(pkt);

        case ESP32_COMM_PROTO_CMD_SAFE_CLOSE:
            return ch32_app_handle_runtime_safe_close(pkt);

        case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
            ch32_app_handle_query_status(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
            ch32_app_handle_read_weight(pkt);
            break;

        default:
            ESP32_Comm_SendProtoNack(pkt->proto_cmd, pkt->proto_seq, ESP32_COMM_ERR_BUSY);
            break;
    }

    return 0U;
}

static uint8_t ch32_app_pump_runtime_commands(void)
{
    ESP32_Comm_Packet_t pkt;

    while(ESP32_Comm_ReadPacket(&pkt) != 0U)
    {
        if(pkt.proto_type != ESP32_COMM_PROTO_TYPE_CMD)
            continue;

        if(ch32_app_handle_runtime_command(&pkt) != 0U)
            return 1U;
    }

    return 0U;
}

// 将长延时拆成小片段，每个片段之间检查紧急命令。
static uint8_t ch32_app_delay_ms_interruptible(uint32_t total_ms)
{
    uint32_t elapsed = 0U;

    while(elapsed < total_ms)
    {
        uint32_t chunk = ACTION_POLL_MS;

        if(ch32_app_pump_runtime_commands() != 0U)
            return 1U;

        if((total_ms - elapsed) < chunk)
            chunk = total_ms - elapsed;

        Delay_Ms(chunk);
        elapsed += chunk;
    }

    return 0U;
}

static uint8_t ch32_app_stepper_move_interruptible(uint8_t dir,
                                                   uint32_t steps,
                                                   uint16_t speed_us)
{
    TMC2209_Enable();
    TMC2209_SetDir(dir);

    for(uint32_t i = 0U; i < steps; i++)
    {
        GPIO_SetBits(TMC_PORT, TMC_STEP_PIN);
        Delay_Us(speed_us);
        GPIO_ResetBits(TMC_PORT, TMC_STEP_PIN);
        Delay_Us(speed_us);

        if((i % STEPPER_ABORT_POLL_STEPS) == 0U)
        {
            if(ch32_app_pump_runtime_commands() != 0U)
            {
                TMC2209_Disable();
                return 1U;
            }
        }
    }

    TMC2209_Disable();
    return 0U;
}

/* ---------- 单个机械动作 ---------- */

static uint8_t ch32_app_open_outer_door(void)
{
    ch32_app_publish_stage(ESP32_COMM_STAGE_DOOR_OPENING, ESP32_COMM_ERR_NONE);

    if(ch32_app_stepper_move_interruptible(DIR_OPEN,
                                           OUTER_DOOR_TEST_STEPS,
                                           OUTER_DOOR_SPEED_US) != 0U)
    {
        return 0U;
    }

    ch32_app_publish_stage(ESP32_COMM_STAGE_DOOR_OPENED, ESP32_COMM_ERR_NONE);
    return 1U;
}

static uint8_t ch32_app_close_outer_door(void)
{
    ch32_app_publish_stage(ESP32_COMM_STAGE_DOOR_CLOSING, ESP32_COMM_ERR_NONE);

    if(ch32_app_stepper_move_interruptible(DIR_CLOSE,
                                           OUTER_DOOR_TEST_STEPS,
                                           OUTER_DOOR_SPEED_US) != 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ch32_app_extend_tray(void)
{
    ch32_app_publish_stage(ESP32_COMM_STAGE_TRAY_EXTENDING, ESP32_COMM_ERR_NONE);
    PushRod_Extend();

    if(ch32_app_delay_ms_interruptible(PUSHROD_EXTEND_TIME_MS) != 0U)
    {
        PushRod_Stop();
        return 0U;
    }

    PushRod_Stop();
    ch32_app_publish_stage(ESP32_COMM_STAGE_TRAY_EXTENDED, ESP32_COMM_ERR_NONE);
    return 1U;
}

static uint8_t ch32_app_retract_tray(void)
{
    ch32_app_publish_stage(ESP32_COMM_STAGE_TRAY_RETRACTING, ESP32_COMM_ERR_NONE);
    PushRod_Retract();

    if(ch32_app_delay_ms_interruptible(PUSHROD_RETRACT_TIME_MS) != 0U)
    {
        PushRod_Stop();
        return 0U;
    }

    PushRod_Stop();
    ch32_app_publish_stage(ESP32_COMM_STAGE_TRAY_RETRACTED, ESP32_COMM_ERR_NONE);
    return 1U;
}

static uint8_t ch32_app_wait_for_cargo(uint32_t timeout_ms)
{
    (void)timeout_ms;
    uint8_t hit_count = 0U;

    ch32_app_publish_stage(ESP32_COMM_STAGE_WAITING_CARGO, ESP32_COMM_ERR_NONE);

    for(;;)
    {
        if(ch32_app_pump_runtime_commands() != 0U)
            return 2U;

        s_app.last_weight_g = ch32_app_read_weight_average(3U);
        ch32_app_publish_status_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                                    ch32_app_active_status_ctx(),
                                    ESP32_COMM_STAGE_WAITING_CARGO,
                                    ESP32_COMM_ERR_NONE);

        if(s_app.last_weight_g >= WEIGHT_TRIGGER_G)
        {
            hit_count++;
            if(hit_count >= WEIGHT_CONFIRM_COUNT)
            {
                ch32_app_publish_stage(ESP32_COMM_STAGE_CARGO_DETECTED,
                                       ESP32_COMM_ERR_NONE);
                return 1U;
            }
        }
        else
        {
            hit_count = 0U;
        }

        if(ch32_app_delay_ms_interruptible(WEIGHT_CHECK_INTERVAL_MS) != 0U)
            return 2U;
    }
}

/* ---------- 安全回收与接驳窗口流程 ---------- */

// 只有 SAFE_CLOSE/天气保护会调用此流程：托盘先收回，再关闭外门并进入锁止阶段。
static uint8_t ch32_app_close_to_safe_locked(uint8_t emit_safe_event)
{
    if(ch32_app_retract_tray() == 0U)
        return 0U;
    if(ch32_app_close_outer_door() == 0U)
        return 0U;

    if(emit_safe_event != 0U)
    {
        ch32_app_publish_stage(ESP32_COMM_STAGE_SAFE_LOCKED, ESP32_COMM_ERR_NONE);
    }
    else
    {
        s_app.current_stage = ESP32_COMM_STAGE_SAFE_LOCKED;
        s_app.locked = 1U;
    }

    return 1U;
}

static uint8_t ch32_app_run_delivery_flow(void)
{
    uint8_t wait_ret;

    if(ch32_app_ensure_hx711_ready() == 0U)
    {
        ch32_app_publish_fault(ESP32_COMM_ERR_SENSOR);
        return 0U;
    }

    if(ch32_app_open_outer_door() == 0U)
        return 0U;
    if(ch32_app_extend_tray() == 0U)
        return 0U;

    wait_ret = ch32_app_wait_for_cargo(WEIGHT_WAIT_TIMEOUT_MS);
    if(wait_ret == 2U)
        return 0U;

    if(ch32_app_close_to_safe_locked(1U) == 0U)
        return 0U;

    ch32_app_publish_stage(ESP32_COMM_STAGE_COMPLETE, ESP32_COMM_ERR_NONE);
    return 1U;
}

/* ---------- 命令分发 ---------- */

// busy 期间只接受不会与当前机械动作冲突的查询或安全命令。
static uint8_t ch32_app_command_allowed_while_busy(uint8_t cmd)
{
    switch(cmd)
    {
        case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
        case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
        case ESP32_COMM_PROTO_CMD_ABORT:
        case ESP32_COMM_PROTO_CMD_SAFE_CLOSE:
            return 1U;

        default:
            return 0U;
    }
}

static void ch32_app_handle_probe_ready(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    ch32_app_publish_idle_status();
}

static void ch32_app_handle_start_dock(const ESP32_Comm_Packet_t *pkt)
{
    ch32_app_begin_action();
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    (void)ch32_app_run_delivery_flow();
    ch32_app_finish_action();
}

static void ch32_app_handle_abort(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    ch32_app_stop_motion();
    ESP32_Comm_FlushRx();
    s_app.action_busy = 0U;
    s_app.action_ctx_valid = 0U;
    s_app.last_error = ESP32_COMM_ERR_NONE;
    s_app.locked = 0U;
    ch32_app_publish_stage(ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
}

static void ch32_app_handle_safe_close(const ESP32_Comm_Packet_t *pkt)
{
    ch32_app_begin_action();
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);

    if((ch32_app_close_to_safe_locked(1U) == 0U) &&
       (s_app.current_stage != ESP32_COMM_STAGE_IDLE))
    {
        ch32_app_publish_fault(ESP32_COMM_ERR_SAFETY);
    }

    ch32_app_finish_action();
}

static void ch32_app_handle_reset_fault(const ESP32_Comm_Packet_t *pkt)
{
    ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
    s_app.last_error = ESP32_COMM_ERR_NONE;
    s_app.locked = 0U;
    s_app.action_ctx_valid = 0U;
    ch32_app_publish_stage(ESP32_COMM_STAGE_READY, ESP32_COMM_ERR_NONE);
}

static void ch32_app_dispatch_command(const ESP32_Comm_Packet_t *pkt)
{
    if(pkt->proto_type != ESP32_COMM_PROTO_TYPE_CMD)
        return;

    if((s_app.action_busy != 0U) &&
       (ch32_app_command_allowed_while_busy(pkt->proto_cmd) == 0U))
    {
        ESP32_Comm_SendProtoNack(pkt->proto_cmd, pkt->proto_seq, ESP32_COMM_ERR_BUSY);
        return;
    }

    switch(pkt->proto_cmd)
    {
        case ESP32_COMM_PROTO_CMD_PROBE_READY:
            ch32_app_handle_probe_ready(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_START_DOCK:
            ch32_app_handle_start_dock(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
            ch32_app_handle_query_status(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
            ch32_app_handle_read_weight(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_ABORT:
            ch32_app_handle_abort(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_SAFE_CLOSE:
            ch32_app_handle_safe_close(pkt);
            break;

        case ESP32_COMM_PROTO_CMD_RESET_FAULT:
            ch32_app_handle_reset_fault(pkt);
            break;

        default:
            ESP32_Comm_SendProtoNack(pkt->proto_cmd,
                                     pkt->proto_seq,
                                     ESP32_COMM_ERR_UNKNOWN_CMD);
            break;
    }
}

void CH32_App_Init(void)
{
    ESP32_Comm_Init();
    TMC2209_Init();
    PushRod_Init();
    HX711_Init();

    ch32_app_stop_motion();

    memset(&s_app, 0, sizeof(s_app));
    s_app.current_stage = ESP32_COMM_STAGE_READY;
    s_app.last_error = ESP32_COMM_ERR_NONE;

    ch32_app_publish_idle_status();
}

// 主循环每次最多处理一个普通命令；无命令时周期广播 READY/FAULT 状态。
void CH32_App_RunOnce(void)
{
    ESP32_Comm_Packet_t pkt;

    if(ESP32_Comm_ReadPacket(&pkt) != 0U)
    {
        s_app.idle_ms = 0U;
        ch32_app_set_request_ctx(&pkt);
        ch32_app_dispatch_command(&pkt);
        return;
    }

    Delay_Ms(10);
    s_app.idle_ms += 10U;

    if((s_app.action_busy == 0U) &&
       (s_app.idle_ms >= READY_BROADCAST_INTERVAL_MS))
    {
        ch32_app_publish_idle_status();
        s_app.idle_ms = 0U;
    }
}
