#include "debug.h"
#include "TMC2209.h"
#include "L298N.h"
#include "HX711.h"
#include "inner_door.h"
#include "esp32_com.h"
#include "ch32_app.h"

#include <string.h>

/*
 * ============================================================================
 * CH32 业务流程层 (精简版)
 *
 * 三层架构:
 *   1) 驱动层: TMC2209 / L298N / HX711 / inner_door
 *   2) 通信层: esp32_com.c (仅二进制协议)
 *   3) 业务流程层: 本文件
 *
 * 已砍掉: 老协议 ASCII 兼容、单步调试命令(OPEN/CLOSE/EXTEND/RETRACT)
 * ============================================================================
 */

/* -------------------- 硬件参数 -------------------- */
#define OUTER_DOOR_TEST_STEPS         75000U
#define OUTER_DOOR_SPEED_US           100U
#define PUSHROD_EXTEND_TIME_MS        2600U
#define PUSHROD_RETRACT_TIME_MS       2600U
#define WEIGHT_TRIGGER_G              20
#define WEIGHT_CHECK_INTERVAL_MS      200U
#define WEIGHT_CONFIRM_COUNT          3U
#define WEIGHT_WAIT_TIMEOUT_MS        15000U
#define READY_BROADCAST_INTERVAL_MS   500U
#define ACTION_POLL_MS                10U
#define STEPPER_ABORT_POLL_STEPS      200U

/* -------------------- 响应上下文 (精简) -------------------- */
typedef struct
{
    uint8_t proto_cmd;
    uint8_t proto_seq;
} ch32_rsp_ctx_t;

/* -------------------- 运行时状态 -------------------- */
static uint8_t s_hx711_ready = 0U;
static uint8_t s_action_busy = 0U;
static uint8_t s_current_stage = ESP32_COMM_STAGE_IDLE;
static uint8_t s_last_error = ESP32_COMM_ERR_NONE;
static uint8_t s_locked = 0U;
static int32_t s_last_weight_g = 0;
static uint32_t s_idle_ms = 0U;

static ch32_rsp_ctx_t s_rsp = {0};
static ch32_rsp_ctx_t s_action_rsp = {0};
static uint8_t s_action_rsp_valid = 0U;

/* -------------------- Flags -------------------- */
static uint16_t build_flags(void)
{
    uint16_t flags = 0U;
    if((s_action_busy == 0U) && (s_last_error == ESP32_COMM_ERR_NONE))
        flags |= ESP32_COMM_FLAG_READY;
    if(s_action_busy != 0U)
        flags |= ESP32_COMM_FLAG_BUSY;
    if(s_locked != 0U)
        flags |= ESP32_COMM_FLAG_LOCKED;
    if(s_last_weight_g >= WEIGHT_TRIGGER_G)
        flags |= ESP32_COMM_FLAG_CARGO_PRESENT;
    return flags;
}

/* -------------------- 上下文与发送辅助 -------------------- */

static void rsp_ctx_set(const ESP32_Comm_Packet_t *pkt)
{
    s_rsp.proto_cmd = pkt->proto_cmd;
    s_rsp.proto_seq = pkt->proto_seq;
}

static const ch32_rsp_ctx_t *active_action_ctx(void)
{
    if(s_action_rsp_valid != 0U) return &s_action_rsp;
    return &s_rsp;
}

static void send_proto_state_ctx(uint8_t proto_type,
                                 const ch32_rsp_ctx_t *ctx,
                                 uint8_t stage,
                                 uint8_t detail)
{
    ESP32_Comm_SendProtoState(proto_type,
                              (ctx != 0) ? ctx->proto_cmd : ESP32_COMM_PROTO_CMD_NONE,
                              (ctx != 0) ? ctx->proto_seq : 0U,
                              stage,
                              detail,
                              build_flags(),
                              s_last_weight_g);
}

/*
 * 发送阶段事件 (STATUS 类型统一承载)
 * 同时更新 busy / locked 状态。
 */
static void send_stage(uint8_t stage, uint8_t detail)
{
    s_current_stage = stage;
    if((stage == ESP32_COMM_STAGE_SAFE_LOCKED) || (stage == ESP32_COMM_STAGE_COMPLETE))
        s_locked = 1U;
    if((stage == ESP32_COMM_STAGE_IDLE) || (stage == ESP32_COMM_STAGE_READY) ||
       (stage == ESP32_COMM_STAGE_SAFE_LOCKED) || (stage == ESP32_COMM_STAGE_COMPLETE) ||
       (stage == ESP32_COMM_STAGE_FAULT))
        s_action_busy = 0U;
    send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS, active_action_ctx(), stage, detail);
}

/* 发送故障并切入 FAULT 阶段 */
static void send_fault(uint8_t err_code)
{
    s_last_error = err_code;
    s_current_stage = ESP32_COMM_STAGE_FAULT;
    s_action_busy = 0U;
    send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS, active_action_ctx(),
                         ESP32_COMM_STAGE_FAULT, err_code);
}

/* 空闲心跳 (合并到 STATUS) */
static void send_idle_heartbeat(void)
{
    if(s_last_error == ESP32_COMM_ERR_NONE)
    {
        s_current_stage = ESP32_COMM_STAGE_READY;
        ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_STATUS,
                                  ESP32_COMM_PROTO_CMD_NONE, 0U,
                                  ESP32_COMM_STAGE_READY,
                                  ESP32_COMM_ERR_NONE,
                                  build_flags(),
                                  s_last_weight_g);
    }
    else
    {
        s_current_stage = ESP32_COMM_STAGE_FAULT;
        ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_STATUS,
                                  ESP32_COMM_PROTO_CMD_NONE, 0U,
                                  ESP32_COMM_STAGE_FAULT,
                                  s_last_error,
                                  build_flags(),
                                  s_last_weight_g);
    }
}

/* 动作开始/结束 */
static void action_begin(void)
{
    s_action_busy = 1U;
    s_locked = 0U;
    s_last_error = ESP32_COMM_ERR_NONE;
    s_action_rsp = s_rsp;
    s_action_rsp_valid = 1U;
}

static void stop_all_action(void)
{
    PushRod_Stop();
    TMC2209_Disable();
}

static void action_end_to_ready(void)
{
    stop_all_action();
    ESP32_Comm_FlushRx();
    s_action_busy = 0U;
    s_action_rsp_valid = 0U;
}

static uint8_t close_to_safe_locked(uint8_t emit_safe_event);

/* -------------------- 称重 -------------------- */
static int32_t get_weight_avg(uint8_t times)
{
    int64_t sum = 0;
    if(times == 0U) times = 1U;
    for(uint8_t i = 0; i < times; i++)
    {
        sum += HX711_Get_Weight();
        Delay_Ms(20);
    }
    s_last_weight_g = (int32_t)(sum / times);
    return s_last_weight_g;
}

static uint8_t ensure_hx711_ready(void)
{
    if(s_hx711_ready != 0U) return 1U;
    if(HX711_Tare() != 0U)
    {
        s_hx711_ready = 1U;
        return 1U;
    }
    return 0U;
}

/* -------------------- 可中断延时与步进 -------------------- */

/* 持续从串口取包，处理运行时可接受的命令 (ABORT/QUERY/WEIGHT)
 * 返回 1 表示被中止 */
static uint8_t pump_runtime_packets(void)
{
    ESP32_Comm_Packet_t pkt;
    while(ESP32_Comm_ReadPacket(&pkt) != 0U)
    {
        if(pkt.proto_type != ESP32_COMM_PROTO_TYPE_CMD)
            continue;

        switch(pkt.proto_cmd)
        {
            case ESP32_COMM_PROTO_CMD_ABORT:
                ESP32_Comm_SendProtoAck(pkt.proto_cmd, pkt.proto_seq);
                stop_all_action();
                s_action_busy = 0U;
                s_action_rsp_valid = 0U;
                s_last_error = ESP32_COMM_ERR_NONE;
                s_locked = 0U;
                s_current_stage = ESP32_COMM_STAGE_IDLE;
                send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                                     &(ch32_rsp_ctx_t){pkt.proto_cmd, pkt.proto_seq},
                                     ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
                return 1U;

            case ESP32_COMM_PROTO_CMD_SAFE_CLOSE:
                ESP32_Comm_SendProtoAck(pkt.proto_cmd, pkt.proto_seq);
                stop_all_action();
                s_action_rsp.proto_cmd = pkt.proto_cmd;
                s_action_rsp.proto_seq = pkt.proto_seq;
                s_action_rsp_valid = 1U;
                s_action_busy = 1U;
                s_locked = 0U;
                s_last_error = ESP32_COMM_ERR_NONE;
                if((close_to_safe_locked(1U) == 0U) && (s_current_stage != ESP32_COMM_STAGE_IDLE))
                {
                    send_fault(ESP32_COMM_ERR_SAFETY);
                }
                action_end_to_ready();
                return 1U;

            case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
                ESP32_Comm_SendProtoAck(pkt.proto_cmd, pkt.proto_seq);
                send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                                     &(ch32_rsp_ctx_t){pkt.proto_cmd, pkt.proto_seq},
                                     s_current_stage, s_last_error);
                break;

            case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
                ESP32_Comm_SendProtoAck(pkt.proto_cmd, pkt.proto_seq);
                if(ensure_hx711_ready() == 0U)
                    send_fault(ESP32_COMM_ERR_SENSOR);
                else
                {
                    s_last_weight_g = get_weight_avg(3U);
                    send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                                         &(ch32_rsp_ctx_t){pkt.proto_cmd, pkt.proto_seq},
                                         s_current_stage, ESP32_COMM_ERR_NONE);
                }
                break;

            default:
                ESP32_Comm_SendProtoNack(pkt.proto_cmd, pkt.proto_seq, ESP32_COMM_ERR_BUSY);
                break;
        }
    }
    return 0U;
}

/* 可中断延时，返回 1=被中止, 0=正常完成 */
static uint8_t delay_ms_with_abort(uint32_t total_ms)
{
    uint32_t elapsed = 0U;
    while(elapsed < total_ms)
    {
        if(pump_runtime_packets() != 0U) return 1U;
        uint32_t chunk = ACTION_POLL_MS;
        if((total_ms - elapsed) < chunk) chunk = total_ms - elapsed;
        Delay_Ms(chunk);
        elapsed += chunk;
    }
    return 0U;
}

/* 可中断步进运动，返回 1=被中止, 0=正常完成 */
static uint8_t stepper_move_interruptible(uint8_t dir, uint32_t steps, uint16_t speed_us)
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
            if(pump_runtime_packets() != 0U)
            {
                TMC2209_Disable();
                return 1U;
            }
        }
    }
    TMC2209_Disable();
    return 0U;
}

/* -------------------- 机构动作 -------------------- */

static uint8_t outer_door_open_test(void)
{
    send_stage(ESP32_COMM_STAGE_DOOR_OPENING, ESP32_COMM_ERR_NONE);
    if(stepper_move_interruptible(DIR_OPEN, OUTER_DOOR_TEST_STEPS, OUTER_DOOR_SPEED_US) != 0U)
        return 0U;
    send_stage(ESP32_COMM_STAGE_DOOR_OPENED, ESP32_COMM_ERR_NONE);
    return 1U;
}

static uint8_t outer_door_close_test(void)
{
    send_stage(ESP32_COMM_STAGE_DOOR_CLOSING, ESP32_COMM_ERR_NONE);
    if(stepper_move_interruptible(DIR_CLOSE, OUTER_DOOR_TEST_STEPS, OUTER_DOOR_SPEED_US) != 0U)
        return 0U;
    return 1U;
}

static uint8_t tray_extend_test(void)
{
    send_stage(ESP32_COMM_STAGE_TRAY_EXTENDING, ESP32_COMM_ERR_NONE);
    PushRod_Extend();
    if(delay_ms_with_abort(PUSHROD_EXTEND_TIME_MS) != 0U)
    {
        PushRod_Stop();
        return 0U;
    }
    PushRod_Stop();
    send_stage(ESP32_COMM_STAGE_TRAY_EXTENDED, ESP32_COMM_ERR_NONE);
    return 1U;
}

static uint8_t tray_retract_test(void)
{
    send_stage(ESP32_COMM_STAGE_TRAY_RETRACTING, ESP32_COMM_ERR_NONE);
    PushRod_Retract();
    if(delay_ms_with_abort(PUSHROD_RETRACT_TIME_MS) != 0U)
    {
        PushRod_Stop();
        return 0U;
    }
    PushRod_Stop();
    send_stage(ESP32_COMM_STAGE_TRAY_RETRACTED, ESP32_COMM_ERR_NONE);
    return 1U;
}

/* 等待货物: 1=检测到, 0=超时, 2=被中止 */
static uint8_t wait_for_payload(uint32_t timeout_ms)
{
    uint32_t elapsed = 0U;
    uint8_t hit_count = 0U;

    send_stage(ESP32_COMM_STAGE_WAITING_CARGO, ESP32_COMM_ERR_NONE);

    while(elapsed < timeout_ms)
    {
        if(pump_runtime_packets() != 0U) return 2U;

        s_last_weight_g = get_weight_avg(3U);

        /* 等待期间持续发状态帧，让 ESP32 拿到最新重量 */
        send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS,
                             active_action_ctx(),
                             ESP32_COMM_STAGE_WAITING_CARGO,
                             ESP32_COMM_ERR_NONE);

        if(s_last_weight_g >= WEIGHT_TRIGGER_G)
        {
            hit_count++;
            if(hit_count >= WEIGHT_CONFIRM_COUNT)
            {
                send_stage(ESP32_COMM_STAGE_CARGO_DETECTED, ESP32_COMM_ERR_NONE);
                return 1U;
            }
        }
        else
        {
            hit_count = 0U;
        }

        if(delay_ms_with_abort(WEIGHT_CHECK_INTERVAL_MS) != 0U) return 2U;
        elapsed += WEIGHT_CHECK_INTERVAL_MS;
    }
    return 0U;
}

/* 收口到安全锁定态 */
static uint8_t close_to_safe_locked(uint8_t emit_safe_event)
{
    if(tray_retract_test() == 0U) return 0U;
    if(outer_door_close_test() == 0U) return 0U;
    if(emit_safe_event != 0U)
        send_stage(ESP32_COMM_STAGE_SAFE_LOCKED, ESP32_COMM_ERR_NONE);
    else
    {
        s_current_stage = ESP32_COMM_STAGE_SAFE_LOCKED;
        s_locked = 1U;
    }
    return 1U;
}

/* 完整配送流程: 开门→伸托盘→等货物→收口→开内门→完成 */
static uint8_t run_delivery_flow(void)
{
    if(ensure_hx711_ready() == 0U)
    {
        send_fault(ESP32_COMM_ERR_SENSOR);
        return 0U;
    }

    if(outer_door_open_test() == 0U) return 0U;
    if(tray_extend_test() == 0U) return 0U;

    uint8_t wait_ret = wait_for_payload(WEIGHT_WAIT_TIMEOUT_MS);
    if(wait_ret == 2U) return 0U;  /* 被中止 */

    if(wait_ret == 0U)
    {
        /* 超时也要收口，保证机构安全 */
        close_to_safe_locked(0U);
        send_fault(ESP32_COMM_ERR_TIMEOUT);
        return 0U;
    }

    /* 正常检测到货物 → 收口 */
    if(close_to_safe_locked(1U) == 0U) return 0U;

    send_stage(ESP32_COMM_STAGE_COMPLETE, ESP32_COMM_ERR_NONE);
    return 1U;
}

/* -------------------- 命令处理 -------------------- */

static void handle_proto_command(const ESP32_Comm_Packet_t *pkt)
{
    if(pkt->proto_type != ESP32_COMM_PROTO_TYPE_CMD)
        return;

    /* busy 时只允许查询/读重/中止 */
    if(s_action_busy != 0U)
    {
        switch(pkt->proto_cmd)
        {
            case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
            case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
            case ESP32_COMM_PROTO_CMD_ABORT:
            case ESP32_COMM_PROTO_CMD_SAFE_CLOSE:
                break;
            default:
                ESP32_Comm_SendProtoNack(pkt->proto_cmd, pkt->proto_seq, ESP32_COMM_ERR_BUSY);
                return;
        }
    }

    switch(pkt->proto_cmd)
    {
        case ESP32_COMM_PROTO_CMD_PROBE_READY:
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            send_idle_heartbeat();
            break;

        case ESP32_COMM_PROTO_CMD_START_DOCK:
            action_begin();
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            (void)run_delivery_flow();
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS, &s_rsp,
                                 s_current_stage, s_last_error);
            break;

        case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            if(ensure_hx711_ready() == 0U)
                send_fault(ESP32_COMM_ERR_SENSOR);
            else
            {
                s_last_weight_g = get_weight_avg(3U);
                send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS, &s_rsp,
                                     s_current_stage, ESP32_COMM_ERR_NONE);
            }
            break;

        case ESP32_COMM_PROTO_CMD_ABORT:
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            stop_all_action();
            ESP32_Comm_FlushRx();
            s_action_busy = 0U;
            s_action_rsp_valid = 0U;
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            send_stage(ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
            break;

        case ESP32_COMM_PROTO_CMD_SAFE_CLOSE:
            action_begin();
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            if((close_to_safe_locked(1U) == 0U) && (s_current_stage != ESP32_COMM_STAGE_IDLE))
            {
                send_fault(ESP32_COMM_ERR_SAFETY);
            }
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_RESET_FAULT:
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            s_action_rsp_valid = 0U;
            send_stage(ESP32_COMM_STAGE_READY, ESP32_COMM_ERR_NONE);
            break;

        case ESP32_COMM_PROTO_CMD_OPEN_INNER_DOOR:
            ESP32_Comm_SendProtoAck(pkt->proto_cmd, pkt->proto_seq);
            InnerDoor_Open();
            send_stage(s_current_stage, ESP32_COMM_ERR_NONE);
            break;

        default:
            ESP32_Comm_SendProtoNack(pkt->proto_cmd, pkt->proto_seq,
                                     ESP32_COMM_ERR_UNKNOWN_CMD);
            break;
    }
}

/* -------------------- 公开接口 -------------------- */

void CH32_App_Init(void)
{
    ESP32_Comm_Init();
    TMC2209_Init();
    PushRod_Init();
    HX711_Init();
    InnerDoor_Init();

    stop_all_action();
    InnerDoor_Close();

    s_hx711_ready = 0U;
    s_action_busy = 0U;
    s_current_stage = ESP32_COMM_STAGE_READY;
    s_last_error = ESP32_COMM_ERR_NONE;
    s_locked = 0U;
    s_last_weight_g = 0;
    s_idle_ms = 0U;
    memset(&s_rsp, 0, sizeof(s_rsp));
    memset(&s_action_rsp, 0, sizeof(s_action_rsp));
    s_action_rsp_valid = 0U;

    send_idle_heartbeat();
}

void CH32_App_RunOnce(void)
{
    ESP32_Comm_Packet_t pkt;

    if(ESP32_Comm_ReadPacket(&pkt) != 0U)
    {
        s_idle_ms = 0U;
        rsp_ctx_set(&pkt);
        handle_proto_command(&pkt);
    }
    else
    {
        Delay_Ms(10);
        s_idle_ms += 10U;

        if((s_action_busy == 0U) && (s_idle_ms >= READY_BROADCAST_INTERVAL_MS))
        {
            send_idle_heartbeat();
            s_idle_ms = 0U;
        }
    }
}
