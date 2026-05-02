#include "debug.h"
#include "TMC2209.h"
#include "L298N.h"
#include "HX711.h"
#include "inner_door.h"
#include "esp32_com.h"
#include "ch32_app.h"

#include <stdio.h>
#include <string.h>

/*
 * ============================================================================
 * 文件说明
 * ----------------------------------------------------------------------------
 * 这个文件是 CH32 侧“业务调度层 / 动作编排层”的核心实现。
 *
 * 你可以把整个 CH32 侧软件分成三层来理解：
 *
 * 1) 驱动层
 *    - TMC2209.c / L298N.c / HX711.c / inner_door.c
 *    - 负责最底层的 GPIO、PWM、步进脉冲、推杆方向、称重采样、舵机角度等。
 *
 * 2) 通信协议层
 *    - esp32_com.c
 *    - 负责从 ESP32 接收串口数据、解析协议帧、打包 ACK/状态帧/错误帧。
 *
 * 3) 业务流程层（当前文件）
 *    - 把“开门 -> 伸托盘 -> 等货物 -> 收托盘 -> 关门 -> 开内门”这类完整动作串起来。
 *    - 负责维护 CH32 当前处于什么阶段、最近一次错误是什么、现在是否忙碌。
 *    - 负责把底层执行结果转成上层可理解的状态反馈。
 *
 * 当前文件的设计目标：
 * - main.c 尽量保持干净，只负责初始化和循环调度。
 * - 所有流程控制、错误处理、动作编排尽量收口到这里。
 * - 后续你如果要接限位、超时保护、异常恢复、正式状态机，优先改这里。
 * ============================================================================
 */

/*
 * 外门步进电机测试步数。
 *
 * 当前代码中并没有接入“真正的开到位 / 关到位”限位传感器，
 * 所以这里只能先使用“固定步数”模拟一次完整的开关门动作。
 *
 * 后续如果你接了：
 * - 开门到位限位开关
 * - 关门到位限位开关
 *
 * 那么这里就应该从“固定步数控制”升级为“限位闭环控制”。
 */
#define OUTER_DOOR_TEST_STEPS         75000U

/*
 * 步进脉冲间隔，单位 us。
 *
 * 数值越小，步进电机越快；
 * 数值越大，步进电机越慢。
 *
 * 这里对应 TMC2209_MoveSteps() / 手搓 STEP 脉冲逻辑中的 Delay_Us(speed_us)。
 */
#define OUTER_DOOR_SPEED_US           100U

/*
 * 推杆伸出总时长，单位 ms。
 *
 * 当前也还是“纯时间控制”，即：
 * - 通电伸出
 * - 延时若干毫秒
 * - 停止
 *
 * 后续推荐升级为：
 * - 伸出限位开关触发即停
 * - 或者加电流检测 / 超时保护
 */
#define PUSHROD_EXTEND_TIME_MS        2600U

/* 推杆缩回总时长，单位 ms。逻辑和上面的伸出时长完全对称。 */
#define PUSHROD_RETRACT_TIME_MS       2600U

/*
 * 判定“货物已放上托盘”的重量阈值，单位 g。
 *
 * 例如：
 * - 小于 20g，认为还没有有效货物
 * - 大于等于 20g，认为可能已经放上货物
 *
 * 注意这里只是“可能”，真正确认还要配合下面的连续确认次数。
 */
#define WEIGHT_TRIGGER_G              20

/*
 * 每次等待货物时，两次重量检测的间隔。
 *
 * 不要太小：
 * - 太小会导致串口调试输出过于频繁
 * - 太小会增加称重抖动影响
 *
 * 不要太大：
 * - 太大可能导致系统响应变迟钝
 */
#define WEIGHT_CHECK_INTERVAL_MS      200U

/*
 * 连续多少次检测都超过阈值，才算真正收货成功。
 *
 * 这是一个简单抗抖策略：
 * - 一次超阈值，可能是噪声
 * - 多次连续超阈值，更可信
 */
#define WEIGHT_CONFIRM_COUNT          3U

/*
 * 最长等待投递超时时间，单位 ms。
 *
 * 也就是：
 * 托盘伸出后，如果在 15s 内还没等到货物，流程就认为超时失败。
 */
#define WEIGHT_WAIT_TIMEOUT_MS        15000U

/*
 * 空闲状态下，CH32 周期性广播“我还活着 / 我已就绪”的时间间隔。
 *
 * 作用：
 * - 让 ESP32 知道 CH32 当前在线
 * - 让上位状态页可以持续显示 READY 心跳
 */
#define READY_BROADCAST_INTERVAL_MS   500U

/*
 * 动作执行期间的最小轮询片段，单位 ms。
 *
 * 为什么需要这个值？
 * 因为很多硬件动作是“阻塞式”的，比如：
 * - 推杆要跑 2600ms
 * - 等待货物要循环很多次
 *
 * 如果完全 Delay_Ms(2600)，就没机会处理中途的 STOP / 查询状态 / 读重量。
 *
 * 所以这里采用“小块延时 + 中途轮询串口命令”的设计，
 * 保证长动作期间仍能响应中止命令。
 */
#define ACTION_POLL_MS                10U

/*
 * 步进电机每发送多少步，插入一次“是否收到中止命令”的轮询。
 *
 * 数值太小：
 * - 响应更快
 * - 但轮询开销更高
 *
 * 数值太大：
 * - 轮询开销小
 * - 但中止响应更慢
 */
#define STEPPER_ABORT_POLL_STEPS      200U

/*
 * 响应上下文。
 *
 * 这个结构体非常关键，它解决了一个常见问题：
 * “当前动作执行完后，我应该回复给哪一条命令？”
 *
 * 因为 CH32 可能收到：
 * - 老协议命令：@A\n、@O\n、@W\n 等
 * - 新协议帧：带 cmd / seq / payload 的二进制协议
 *
 * 所以这里统一记录本次动作的“回复来源”：
 * - is_proto   : 当前上下文是不是新协议
 * - proto_cmd  : 新协议命令码
 * - proto_seq  : 新协议序号，用于 ACK / NACK 对应匹配
 * - legacy_cmd : 老协议命令字符
 */
typedef struct
{
    uint8_t is_proto;   /* 1 = 新协议帧；0 = 老的 ASCII 命令帧 */
    uint8_t proto_cmd;  /* 新协议命令码，例如 START_DOCK / QUERY_STATUS */
    uint8_t proto_seq;  /* 新协议序列号，用于和 ESP32 做请求-响应匹配 */
    char legacy_cmd;    /* 老协议命令字符，例如 'A' / 'O' / 'W' */
} ch32_rsp_ctx_t;

/*
 * =============================
 * 全局静态状态变量
 * =============================
 * 这一组变量可以理解为“CH32 的运行时状态寄存器”。
 *
 * 由于当前项目还没有上 RTOS，也没有专门的状态机对象，
 * 所以使用 static 文件内变量来保存运行状态。
 *
 * 优点：
 * - 简单直接
 * - 逻辑集中
 *
 * 后续如果状态机复杂了，可以再封装成一个 app_context 结构体。
 */

static uint8_t s_hx711_ready = 0U;                  /* HX711 是否已经完成去皮 / 就绪 */
static uint8_t s_action_busy = 0U;                  /* 当前是否有“长动作”正在执行 */
static uint8_t s_current_stage = ESP32_COMM_STAGE_IDLE; /* 当前业务阶段 */
static uint8_t s_last_error = ESP32_COMM_ERR_NONE;  /* 最近一次错误码 */
static uint8_t s_locked = 0U;                       /* 当前系统是否处于安全锁定状态 */
static int32_t s_last_weight_g = 0;                 /* 最近一次测得的重量值，单位克 */
static uint32_t s_idle_ms = 0U;                     /* 空闲累计时间，用于 READY 心跳广播 */

/*
 * s_rsp：
 *   保存“刚刚收到的那一帧命令”的上下文。
 *
 * s_action_rsp：
 *   保存“当前正在执行的动作”对应的上下文。
 *   例如：
 *   收到 START_DOCK 后，会进入一整套较长流程，
 *   即使中间又插入查询状态，也不能丢掉最初那条动作命令的响应上下文。
 *
 * s_action_rsp_valid：
 *   说明 s_action_rsp 当前是否有效。
 */
static ch32_rsp_ctx_t s_rsp = {0};
static ch32_rsp_ctx_t s_action_rsp = {0};
static uint8_t s_action_rsp_valid = 0U;

/*
 * 把新协议命令码映射为老协议字符。
 *
 * 这样做的意义：
 * 1. CH32 内部同时兼容“新二进制协议”和“旧 ASCII 协议”
 * 2. 即使主控已经切到新协议，日志和兼容输出仍能继续沿用旧 ACK 格式
 *
 * 例如：
 * - START_DOCK -> 'A'
 * - OPEN_DOOR  -> 'O'
 */
static char proto_cmd_to_legacy(uint8_t proto_cmd)
{
    switch(proto_cmd)
    {
        case ESP32_COMM_PROTO_CMD_PROBE_READY:  return 'P';
        case ESP32_COMM_PROTO_CMD_START_DOCK:   return 'A';
        case ESP32_COMM_PROTO_CMD_OPEN_DOOR:    return 'O';
        case ESP32_COMM_PROTO_CMD_CLOSE_DOOR:   return 'C';
        case ESP32_COMM_PROTO_CMD_EXTEND_TRAY:  return 'E';
        case ESP32_COMM_PROTO_CMD_RETRACT_TRAY: return 'R';
        case ESP32_COMM_PROTO_CMD_QUERY_STATUS: return 'I';
        case ESP32_COMM_PROTO_CMD_RESET_FAULT:  return 'K';
        case ESP32_COMM_PROTO_CMD_READ_WEIGHT:  return 'W';
        case ESP32_COMM_PROTO_CMD_ABORT:        return 'S';
        default:                                return 0;
    }
}

/*
 * 把阶段码转换为可读字符串。
 *
 * 这个函数主要用于：
 * - 串口调试输出
 * - 老协议文本状态上报
 *
 * 注意：
 * 它只是“展示层转换”，不会改变任何业务逻辑。
 */
static const char *stage_name(uint8_t stage)
{
    switch(stage)
    {
        case ESP32_COMM_STAGE_IDLE:            return "IDLE";
        case ESP32_COMM_STAGE_READY:           return "READY";
        case ESP32_COMM_STAGE_DOOR_OPENING:    return "DOOR_OPENING";
        case ESP32_COMM_STAGE_DOOR_OPENED:     return "DOOR_OPENED";
        case ESP32_COMM_STAGE_TRAY_EXTENDING:  return "TRAY_EXTENDING";
        case ESP32_COMM_STAGE_TRAY_EXTENDED:   return "TRAY_EXTENDED";
        case ESP32_COMM_STAGE_WAITING_CARGO:   return "WAITING_CARGO";
        case ESP32_COMM_STAGE_CARGO_DETECTED:  return "CARGO_DETECTED";
        case ESP32_COMM_STAGE_TRAY_RETRACTING: return "TRAY_RETRACTING";
        case ESP32_COMM_STAGE_TRAY_RETRACTED:  return "TRAY_RETRACTED";
        case ESP32_COMM_STAGE_DOOR_CLOSING:    return "DOOR_CLOSING";
        case ESP32_COMM_STAGE_SAFE_LOCKED:     return "SAFE_LOCKED";
        case ESP32_COMM_STAGE_COMPLETE:        return "COMPLETE";
        case ESP32_COMM_STAGE_FAULT:           return "FAULT";
        default:                               return "UNKNOWN";
    }
}

/*
 * 把错误码转换为可读字符串。
 *
 * 作用同样是给日志和上位显示使用。
 * 例如：
 * - 1 -> TIMEOUT
 * - 5 -> SENSOR
 */
static const char *error_name(uint8_t err)
{
    switch(err)
    {
        case ESP32_COMM_ERR_NONE:        return "NONE";
        case ESP32_COMM_ERR_TIMEOUT:     return "TIMEOUT";
        case ESP32_COMM_ERR_LIMIT:       return "LIMIT";
        case ESP32_COMM_ERR_WEIGHT:      return "WEIGHT";
        case ESP32_COMM_ERR_JAM:         return "JAM";
        case ESP32_COMM_ERR_SENSOR:      return "SENSOR";
        case ESP32_COMM_ERR_SAFETY:      return "SAFETY";
        case ESP32_COMM_ERR_BUSY:        return "BUSY";
        case ESP32_COMM_ERR_UNKNOWN_CMD: return "UNKNOWN_CMD";
        case ESP32_COMM_ERR_BAD_CRC:     return "BAD_CRC";
        case ESP32_COMM_ERR_INTERNAL:    return "INTERNAL";
        default:                         return "ERR";
    }
}

/*
 * 根据当前内部状态，拼出一个 flags 位图。
 *
 * 这个 flags 会被放进二进制协议帧里发给 ESP32，
 * 让主控侧能快速知道 CH32 当前是否：
 * - ready
 * - busy
 * - locked
 * - cargo_present
 *
 * 注意这里不是“原始传感器值”，而是“高层逻辑状态”。
 */
static uint16_t build_flags(void)
{
    uint16_t flags = 0U;

    /*
     * 只有在：
     * - 当前没有动作执行
     * - 当前没有故障
     * 时，才认为系统处于 ready 状态。
     */
    if((s_action_busy == 0U) && (s_last_error == ESP32_COMM_ERR_NONE))
    {
        flags |= ESP32_COMM_FLAG_READY;
    }

    /* 系统正在执行长动作，比如开门 / 收货 / 关门。 */
    if(s_action_busy != 0U)
    {
        flags |= ESP32_COMM_FLAG_BUSY;
    }

    /* 当前处于安全锁定态，比如已经关门并锁住。 */
    if(s_locked != 0U)
    {
        flags |= ESP32_COMM_FLAG_LOCKED;
    }

    /* 最近一次重量值已经达到载货判定阈值。 */
    if(s_last_weight_g >= WEIGHT_TRIGGER_G)
    {
        flags |= ESP32_COMM_FLAG_CARGO_PRESENT;
    }

    return flags;
}

/*
 * 把刚收到的数据包 pkt 中的“回复上下文”提取到 s_rsp。
 *
 * 为什么不直接用 pkt？
 * 因为后面很多函数不一定直接拿得到原始 pkt，
 * 所以这里先做一份“当前请求上下文”的缓存。
 */
static void rsp_ctx_set_from_packet(const ESP32_Comm_Packet_t *pkt)
{
    s_rsp.is_proto = pkt->is_proto;
    s_rsp.proto_cmd = pkt->proto_cmd;
    s_rsp.proto_seq = pkt->proto_seq;
    s_rsp.legacy_cmd = pkt->legacy_cmd;
}

/*
 * 向指定上下文发送 ACK。
 *
 * 兼容策略：
 * - 如果上下文来自新协议：
 *   先发新协议 ACK，再尝试附带发一个老协议 ACK 文本，方便调试兼容。
 * - 如果上下文来自老协议：
 *   只发老协议 ACK。
 */
static void send_ack_for_ctx(const ch32_rsp_ctx_t *ctx)
{
    char legacy = 0;

    /* 入参保护，防止空指针。 */
    if(ctx == 0)
    {
        return;
    }

    if(ctx->is_proto != 0U)
    {
        /*
         * 新协议 ACK：
         * 告诉 ESP32，这条命令已经被 CH32 正确接收并接受处理。
         */
        ESP32_Comm_SendProtoAck(ctx->proto_cmd, ctx->proto_seq);

        /*
         * 再额外转成旧命令字符发送一份 ASCII ACK，
         * 便于串口助手直接观察。
         */
        legacy = proto_cmd_to_legacy(ctx->proto_cmd);
        if(legacy != 0)
        {
            ESP32_Comm_SendLegacyAck(legacy);
        }
    }
    else if(ctx->legacy_cmd != 0)
    {
        /* 老协议命令只需要直接回复 [ACK] X */
        ESP32_Comm_SendLegacyAck(ctx->legacy_cmd);
    }
}

/*
 * 针对“当前请求上下文 s_rsp”发送 ACK。
 *
 * 这是一个便捷包装函数，避免每次都显式传 &s_rsp。
 */
static void send_ack_current(void)
{
    send_ack_for_ctx(&s_rsp);
}

/*
 * 向指定上下文发送 NACK（否认 / 拒绝处理）。
 *
 * 适用于：
 * - 忙碌状态拒绝新的阻塞命令
 * - 未知命令
 * - 参数非法
 * - CRC 错误等
 *
 * 这里会同时：
 * - 对新协议发 NACK 帧
 * - 对串口调试发老协议文本错误
 */
static void send_nack_for_ctx(const ch32_rsp_ctx_t *ctx, uint8_t err_code)
{
    char legacy[40];

    if((ctx != 0) && (ctx->is_proto != 0U))
    {
        ESP32_Comm_SendProtoNack(ctx->proto_cmd, ctx->proto_seq, err_code);
    }

    snprintf(legacy, sizeof(legacy), "%s", error_name(err_code));
    ESP32_Comm_SendLegacyError(legacy);
}

/* 针对当前请求上下文发送 NACK。 */
static void send_nack_current(uint8_t err_code)
{
    send_nack_for_ctx(&s_rsp, err_code);
}

/*
 * 发送一帧“状态类协议帧”，但上下文由调用者指定。
 *
 * proto_type：
 * - STATUS    : 普通状态回报
 * - EVENT     : 某个阶段切换事件
 * - HEARTBEAT : 心跳
 * - ERROR     : 错误事件
 *
 * ctx：
 * - 如果来自新协议请求，帧内会带上 cmd / seq
 * - 如果不是，就用 NONE / 0 填充
 */
static void send_proto_state_ctx(uint8_t proto_type,
                                 const ch32_rsp_ctx_t *ctx,
                                 uint8_t stage,
                                 uint8_t detail)
{
    ESP32_Comm_SendProtoState(proto_type,
                              ((ctx != 0) && (ctx->is_proto != 0U)) ? ctx->proto_cmd : ESP32_COMM_PROTO_CMD_NONE,
                              ((ctx != 0) && (ctx->is_proto != 0U)) ? ctx->proto_seq : 0U,
                              stage,
                              detail,
                              build_flags(),
                              s_last_weight_g);
}

/*
 * 获取“当前动作应该使用哪个响应上下文”。
 *
 * 情况说明：
 * - 如果当前正在执行一个长动作，那么应该优先用 s_action_rsp
 * - 如果没有，则退化到当前请求 s_rsp
 *
 * 例如：
 * START_DOCK 执行到一半时，期间可能还收到状态查询。
 * 但流程事件（比如 DOOR_OPENED）应该仍然归属 START_DOCK 这条动作命令。
 */
static const ch32_rsp_ctx_t *active_action_ctx(void)
{
    if(s_action_rsp_valid != 0U)
    {
        return &s_action_rsp;
    }
    return &s_rsp;
}

/*
 * 发送阶段切换事件，并同步更新内部状态。
 *
 * 这个函数非常核心，它做了三件事：
 * 1. 更新 s_current_stage
 * 2. 按阶段语义修正 busy / locked
 * 3. 向 ESP32 和老协议串口同时广播状态
 *
 * detail 参数通常传：
 * - ESP32_COMM_ERR_NONE  表示正常阶段切换
 * - 某个错误码          表示故障细节
 */
static void send_stage(uint8_t proto_type, uint8_t stage, uint8_t detail)
{
    char line[96];
    const ch32_rsp_ctx_t *ctx = active_action_ctx();

    /* 先把当前阶段更新到全局状态。 */
    s_current_stage = stage;

    /*
     * 如果流程已经走到了 SAFE_LOCKED / COMPLETE，
     * 说明当前结构上已经收口完成，可以视为“锁定态”。
     */
    if((stage == ESP32_COMM_STAGE_SAFE_LOCKED) || (stage == ESP32_COMM_STAGE_COMPLETE))
    {
        s_locked = 1U;
    }

    /*
     * 某些阶段意味着“当前动作已经结束”，
     * 到了这些阶段时，应主动清 busy。
     */
    if((stage == ESP32_COMM_STAGE_IDLE) || (stage == ESP32_COMM_STAGE_READY) ||
       (stage == ESP32_COMM_STAGE_SAFE_LOCKED) || (stage == ESP32_COMM_STAGE_COMPLETE) ||
       (stage == ESP32_COMM_STAGE_FAULT))
    {
        s_action_busy = 0U;
    }

    /* 发二进制状态帧给 ESP32。 */
    send_proto_state_ctx(proto_type, ctx, stage, detail);

    /* 再发一份文本状态给串口调试。 */
    snprintf(line, sizeof(line), "STAGE=%s", stage_name(stage));
    ESP32_Comm_SendLegacyStatus(line);
}

/*
 * 发送故障事件，并把系统状态切换到 FAULT。
 *
 * 行为包括：
 * - 记录最近一次错误码
 * - 切换到 FAULT 阶段
 * - 清 busy
 * - 向 ESP32 上报 ERROR 类型协议帧
 * - 向老协议输出错误文本
 */
static void send_fault(uint8_t err_code, const char *legacy_text)
{
    char line[96];
    const ch32_rsp_ctx_t *ctx = active_action_ctx();

    s_last_error = err_code;
    s_current_stage = ESP32_COMM_STAGE_FAULT;
    s_action_busy = 0U;

    /* 向 ESP32 发送明确的错误事件帧。 */
    send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_ERROR, ctx, ESP32_COMM_STAGE_FAULT, err_code);

    /*
     * 如果调用方提供了更具体的文本，就优先发它；
     * 否则退回到通用错误码名字。
     */
    if((legacy_text != 0) && (legacy_text[0] != '\0'))
    {
        ESP32_Comm_SendLegacyError(legacy_text);
    }
    else
    {
        snprintf(line, sizeof(line), "%s", error_name(err_code));
        ESP32_Comm_SendLegacyError(line);
    }
}

/*
 * 空闲状态下发送心跳。
 *
 * 正常时：
 * - 阶段会显示 READY
 * - 文本输出 CH32_READY
 *
 * 故障时：
 * - 阶段会显示 FAULT
 * - detail 带最近一次错误码
 *
 * 这样 ESP32 即使没有主动查询，也能从 CH32 心跳知道当前健康状态。
 */
static void send_idle_heartbeat(void)
{
    if(s_last_error == ESP32_COMM_ERR_NONE)
    {
        s_current_stage = ESP32_COMM_STAGE_READY;
        ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_HEARTBEAT,
                                  ESP32_COMM_PROTO_CMD_NONE,
                                  0U,
                                  ESP32_COMM_STAGE_READY,
                                  ESP32_COMM_ERR_NONE,
                                  build_flags(),
                                  s_last_weight_g);
        ESP32_Comm_SendLegacyStatus("CH32_READY");
    }
    else
    {
        s_current_stage = ESP32_COMM_STAGE_FAULT;
        ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_HEARTBEAT,
                                  ESP32_COMM_PROTO_CMD_NONE,
                                  0U,
                                  ESP32_COMM_STAGE_FAULT,
                                  s_last_error,
                                  build_flags(),
                                  s_last_weight_g);
    }
}

/*
 * 停止所有运动执行器。
 *
 * 当前包含：
 * - 推杆停止
 * - 步进驱动失能
 *
 * 注意：
 * 这只是“输出停止”层面的安全动作，
 * 不代表一定完成了业务流程复位。
 */
static void stop_all_action(void)
{
    PushRod_Stop();
    TMC2209_Disable();
}

/*
 * 标记“开始进入一个长动作流程”。
 *
 * 作用：
 * - 置 busy
 * - 清锁定态
 * - 清除上一次错误
 * - 记住当前这次动作的回复上下文
 */
static void action_begin(void)
{
    s_action_busy = 1U;
    s_locked = 0U;
    s_last_error = ESP32_COMM_ERR_NONE;
    s_action_rsp = s_rsp;
    s_action_rsp_valid = 1U;
}

/*
 * 动作执行结束后，回到“可继续接命令”的状态。
 *
 * 这里做了几件清尾工作：
 * - 关闭全部执行器，防止残留动作
 * - 清空接收缓存，避免旧数据继续污染当前流程
 * - busy 清零
 * - 当前动作上下文失效
 *
 * 注意：
 * 这不一定把阶段改回 READY，阶段仍由具体流程中的 send_stage() 决定。
 */
static void action_end_to_ready(void)
{
    stop_all_action();
    ESP32_Comm_FlushRx();
    s_action_busy = 0U;
    s_action_rsp_valid = 0U;
}

/*
 * 判断某个老协议命令是否属于“会占用较长时间的阻塞动作命令”。
 *
 * 这些命令包括：
 * - A 启动整套收货流程
 * - O 开门
 * - C 关门
 * - E 伸托盘
 * - R 收托盘
 *
 * 若系统已经 busy，这些命令应直接拒绝，避免动作重入。
 */
static uint8_t is_blocking_legacy_cmd(char cmd)
{
    switch(cmd)
    {
        case 'A':
        case 'O':
        case 'C':
        case 'E':
        case 'R':
            return 1U;
        default:
            return 0U;
    }
}

/*
 * 读取若干次重量并求平均值。
 *
 * 设计目的：
 * - 单次 HX711 读数可能有波动
 * - 简单平均可以让显示和判定更稳定
 *
 * times == 0 时自动纠正为 1，防止除 0。
 */
static int32_t get_weight_avg(uint8_t times)
{
    int64_t sum = 0;
    uint8_t i = 0;

    if(times == 0U)
    {
        times = 1U;
    }

    for(i = 0; i < times; i++)
    {
        sum += HX711_Get_Weight();
        Delay_Ms(20);
    }

    s_last_weight_g = (int32_t)(sum / times);
    return s_last_weight_g;
}

/*
 * 确保 HX711 已经准备好。
 *
 * 当前策略：
 * - 如果之前已经成功去皮，则直接认为 ready
 * - 否则现在尝试执行一次 HX711_Tare()
 * - 成功后记住 ready 状态，避免重复去皮
 *
 * 这能减少每次读重量前都重新初始化的开销。
 */
static uint8_t ensure_hx711_ready(void)
{
    if(s_hx711_ready != 0U)
    {
        return 1U;
    }

    if(HX711_Tare() != 0U)
    {
        s_hx711_ready = 1U;
        ESP32_Comm_SendLegacyStatus("HX711_READY");
        return 1U;
    }

    return 0U;
}

/*
 * 按指定上下文发送“当前状态”。
 *
 * 一般用于：
 * - QUERY_STATUS 命令
 * - busy 过程中被动查询
 */
static void send_status_for_ctx(const ch32_rsp_ctx_t *ctx)
{
    char text[64];

    send_ack_for_ctx(ctx);
    send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS, ctx, s_current_stage, s_last_error);
    snprintf(text, sizeof(text), "STAGE=%s", stage_name(s_current_stage));
    ESP32_Comm_SendLegacyStatus(text);
}

/*
 * 按指定上下文发送“当前重量”。
 *
 * 步骤：
 * 1. 先 ACK 请求
 * 2. 确保 HX711 已就绪
 * 3. 多次平均读取重量
 * 4. 发状态帧 + 文本重量
 */
static void send_weight_for_ctx(const ch32_rsp_ctx_t *ctx)
{
    char text[48];

    send_ack_for_ctx(ctx);
    if(ensure_hx711_ready() == 0U)
    {
        s_last_error = ESP32_COMM_ERR_SENSOR;
        send_fault(ESP32_COMM_ERR_SENSOR, "HX711_NOT_READY");
        return;
    }

    s_last_weight_g = get_weight_avg(3U);
    send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_STATUS, ctx, s_current_stage, ESP32_COMM_ERR_NONE);
    snprintf(text, sizeof(text), "WEIGHT=%ld", (long)s_last_weight_g);
    ESP32_Comm_SendLegacyStatus(text);
}

/*
 * 处理“动作执行期间”插入进来的命令。
 *
 * 这个函数只处理那些允许运行时打断 / 查询的命令：
 * - ABORT / S
 * - QUERY_STATUS / I
 * - READ_WEIGHT / W
 *
 * 返回值：
 * - 1：表示收到“应该中止当前长动作”的命令
 * - 0：表示未中止，只是处理了查询/读重量等
 */
static uint8_t handle_runtime_packet(const ESP32_Comm_Packet_t *pkt)
{
    ch32_rsp_ctx_t ctx = {0};

    /* 先把局部上下文从数据包中提取出来，避免直接依赖全局 s_rsp。 */
    ctx.is_proto = pkt->is_proto;
    ctx.proto_cmd = pkt->proto_cmd;
    ctx.proto_seq = pkt->proto_seq;
    ctx.legacy_cmd = pkt->legacy_cmd;

    if(pkt->is_proto != 0U)
    {
        /* 动作期间只接收新协议中的 CMD 帧，其它类型直接忽略。 */
        if(pkt->proto_type != ESP32_COMM_PROTO_TYPE_CMD)
        {
            return 0U;
        }

        switch(pkt->proto_cmd)
        {
            case ESP32_COMM_PROTO_CMD_ABORT:
                /*
                 * 立即响应停止：
                 * - ACK
                 * - 停止执行器
                 * - 清 busy / 清错误 / 解锁
                 * - 阶段切回 IDLE
                 */
                send_ack_for_ctx(&ctx);
                stop_all_action();
                s_action_busy = 0U;
                s_action_rsp_valid = 0U;
                s_last_error = ESP32_COMM_ERR_NONE;
                s_locked = 0U;
                s_current_stage = ESP32_COMM_STAGE_IDLE;
                send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_EVENT, &ctx, ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
                ESP32_Comm_SendLegacyStatus("STOP_OK");
                return 1U;

            case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
                send_status_for_ctx(&ctx);
                return 0U;

            case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
                send_weight_for_ctx(&ctx);
                return 0U;

            default:
                /* 动作期间，其他命令一律视为不允许插入。 */
                send_nack_for_ctx(&ctx, ESP32_COMM_ERR_BUSY);
                return 0U;
        }
    }

    /* 老协议下的运行时命令处理逻辑。 */
    switch(pkt->legacy_cmd)
    {
        case 'S':
            send_ack_for_ctx(&ctx);
            stop_all_action();
            s_action_busy = 0U;
            s_action_rsp_valid = 0U;
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
            ESP32_Comm_SendLegacyStatus("STOP_OK");
            return 1U;

        case 'I':
            send_status_for_ctx(&ctx);
            return 0U;

        case 'W':
            send_weight_for_ctx(&ctx);
            return 0U;

        default:
            ESP32_Comm_SendLegacyError("BUSY");
            return 0U;
    }
}

/*
 * 持续从串口接收缓冲中提取数据包，并尝试处理运行时命令。
 *
 * 只要有包，就一直处理到缓冲区空。
 * 只要遇到一个“中止型命令”，就立即返回 1。
 */
static uint8_t pump_runtime_packets(void)
{
    ESP32_Comm_Packet_t pkt;

    while(ESP32_Comm_ReadPacket(&pkt) != 0U)
    {
        if(handle_runtime_packet(&pkt) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

/*
 * 可中断的毫秒延时。
 *
 * 这是把普通 Delay_Ms(total_ms) 拆成很多小片段：
 * - 每隔 ACTION_POLL_MS 轮询一次串口
 * - 如果收到 STOP / ABORT，就提前退出
 *
 * 返回值：
 * - 1：延时期间被中止
 * - 0：正常延时完成
 */
static uint8_t delay_ms_with_abort(uint32_t total_ms)
{
    uint32_t elapsed = 0U;
    uint32_t chunk = ACTION_POLL_MS;

    while(elapsed < total_ms)
    {
        if(pump_runtime_packets() != 0U)
        {
            return 1U;
        }

        if((total_ms - elapsed) < chunk)
        {
            chunk = total_ms - elapsed;
        }

        Delay_Ms(chunk);
        elapsed += chunk;
    }

    return 0U;
}

/*
 * 可中断的步进电机运动函数。
 *
 * 处理流程：
 * 1. 使能 TMC2209
 * 2. 设置方向
 * 3. 手动输出 steps 个 STEP 脉冲
 * 4. 每隔若干步检查一次是否收到中止命令
 * 5. 最终关闭驱动
 *
 * 返回值：
 * - 1：中途被中止
 * - 0：正常运动完成
 */
static uint8_t stepper_move_interruptible(uint8_t dir, uint32_t steps, uint16_t speed_us)
{
    uint32_t i;

    TMC2209_Enable();
    TMC2209_SetDir(dir);

    for(i = 0U; i < steps; i++)
    {
        /* STEP 上升沿 / 高电平脉冲 */
        GPIO_SetBits(TMC_PORT, TMC_STEP_PIN);
        Delay_Us(speed_us);

        /* STEP 拉低，完成一个完整步进脉冲周期 */
        GPIO_ResetBits(TMC_PORT, TMC_STEP_PIN);
        Delay_Us(speed_us);

        /* 按设定步数间隔插入一次运行时命令轮询。 */
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

/*
 * 等待货物放到托盘上。
 *
 * 返回值定义：
 * - 1：成功检测到货物
 * - 0：超时，没等到货物
 * - 2：等待期间被中止
 *
 * 核心逻辑：
 * - 周期性测量重量
 * - 连续多次超过阈值才算真正检测到
 * - 每轮等待中也允许响应中止命令
 */
static uint8_t wait_for_payload(uint32_t timeout_ms)
{
    uint32_t elapsed = 0U;
    uint8_t hit_count = 0U;
    char dbg[48];

    /* 一进入等待区，就先切阶段。 */
    send_stage(ESP32_COMM_PROTO_TYPE_STATUS, ESP32_COMM_STAGE_WAITING_CARGO, ESP32_COMM_ERR_NONE);

    while(elapsed < timeout_ms)
    {
        if(pump_runtime_packets() != 0U)
        {
            return 2U;
        }

        /* 读取当前平均重量。 */
        s_last_weight_g = get_weight_avg(3U);

        /* 调试输出，便于你在串口助手里直接看到重量变化曲线。 */
        snprintf(dbg, sizeof(dbg), "WEIGHT=%ld", (long)s_last_weight_g);
        ESP32_Comm_SendLegacyDebug(dbg);

        /*
         * 这里发的是心跳型状态帧：
         * 让 ESP32 在等待货物阶段也能持续获得最新状态和重量。
         */
        send_proto_state_ctx(ESP32_COMM_PROTO_TYPE_HEARTBEAT,
                             active_action_ctx(),
                             ESP32_COMM_STAGE_WAITING_CARGO,
                             ESP32_COMM_ERR_NONE);

        if(s_last_weight_g >= WEIGHT_TRIGGER_G)
        {
            hit_count++;
            if(hit_count >= WEIGHT_CONFIRM_COUNT)
            {
                send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_CARGO_DETECTED, ESP32_COMM_ERR_NONE);
                ESP32_Comm_SendLegacyStatus("PAYLOAD_OK");
                return 1U;
            }
        }
        else
        {
            /*
             * 只要中间有一次掉回阈值以下，就重新累计。
             * 这是一种简单但有效的抗抖策略。
             */
            hit_count = 0U;
        }

        if(delay_ms_with_abort(WEIGHT_CHECK_INTERVAL_MS) != 0U)
        {
            return 2U;
        }
        elapsed += WEIGHT_CHECK_INTERVAL_MS;
    }

    return 0U;
}

/*
 * 外门打开测试流程。
 *
 * 当前使用固定步数模拟开门，没有真实限位闭环。
 * 后续建议替换成：
 * - 直到“开门到位限位”触发
 * - 或者“达到最大安全步数后失败退出”
 */
static uint8_t outer_door_open_test(void)
{
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_DOOR_OPENING, ESP32_COMM_ERR_NONE);
    if(stepper_move_interruptible(DIR_OPEN, OUTER_DOOR_TEST_STEPS, OUTER_DOOR_SPEED_US) != 0U)
    {
        return 0U;
    }
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_DOOR_OPENED, ESP32_COMM_ERR_NONE);
    ESP32_Comm_SendLegacyStatus("OUTER_OPEN_OK");
    return 1U;
}

/*
 * 外门关闭测试流程。
 *
 * 同样还是固定步数版本，后续可升级为限位闭环。
 */
static uint8_t outer_door_close_test(void)
{
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_DOOR_CLOSING, ESP32_COMM_ERR_NONE);
    if(stepper_move_interruptible(DIR_CLOSE, OUTER_DOOR_TEST_STEPS, OUTER_DOOR_SPEED_US) != 0U)
    {
        return 0U;
    }
    ESP32_Comm_SendLegacyStatus("OUTER_CLOSE_OK");
    return 1U;
}

/*
 * 托盘伸出测试流程。
 *
 * 当前版本：
 * - 推杆伸出
 * - 延时固定时间
 * - 推杆停止
 *
 * 期间允许被中止。
 */
static uint8_t tray_extend_test(void)
{
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_TRAY_EXTENDING, ESP32_COMM_ERR_NONE);
    PushRod_Extend();
    if(delay_ms_with_abort(PUSHROD_EXTEND_TIME_MS) != 0U)
    {
        PushRod_Stop();
        return 0U;
    }
    PushRod_Stop();
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_TRAY_EXTENDED, ESP32_COMM_ERR_NONE);
    ESP32_Comm_SendLegacyStatus("TRAY_EXTEND_OK");
    return 1U;
}

/*
 * 托盘回收测试流程。
 *
 * 当前版本仍是时间控制，逻辑和伸出对称。
 */
static uint8_t tray_retract_test(void)
{
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_TRAY_RETRACTING, ESP32_COMM_ERR_NONE);
    PushRod_Retract();
    if(delay_ms_with_abort(PUSHROD_RETRACT_TIME_MS) != 0U)
    {
        PushRod_Stop();
        return 0U;
    }
    PushRod_Stop();
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_TRAY_RETRACTED, ESP32_COMM_ERR_NONE);
    ESP32_Comm_SendLegacyStatus("TRAY_RETRACT_OK");
    return 1U;
}

/*
 * 从当前状态“收口”到安全锁定态。
 *
 * 统一动作：
 * 1. 先收回托盘
 * 2. 再关闭外门
 * 3. 根据调用场景选择是否上报 SAFE_LOCKED
 *
 * 这样可以把“失败场景 / 超时场景 / 正常场景”的后段收尾逻辑收敛成一个函数。
 * 超时失败时不先广播正常 SAFE_LOCKED，避免 ESP32 把本次流程误判为完成。
 */
static uint8_t close_to_safe_locked(uint8_t emit_safe_event)
{
    if(tray_retract_test() == 0U)
    {
        return 0U;
    }
    if(outer_door_close_test() == 0U)
    {
        return 0U;
    }
    if(emit_safe_event != 0U)
    {
        send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_SAFE_LOCKED, ESP32_COMM_ERR_NONE);
    }
    else
    {
        s_current_stage = ESP32_COMM_STAGE_SAFE_LOCKED;
        s_locked = 1U;
    }
    return 1U;
}

/*
 * 运行完整的配送接收流程。
 *
 * 流程顺序：
 * 1. 确保 HX711 就绪
 * 2. 外门打开
 * 3. 托盘伸出
 * 4. 等待货物
 * 5. 收托盘 + 关外门 -> SAFE_LOCKED
 * 6. 打开内门
 * 7. 流程完成 -> COMPLETE
 *
 * 返回值：
 * - 1：整套流程成功结束
 * - 0：流程失败 / 中止 / 超时
 */
static uint8_t run_delivery_flow(void)
{
    uint8_t wait_ret = 0U;

    /* 收货流程依赖称重检测，所以先确保 HX711 可用。 */
    if(ensure_hx711_ready() == 0U)
    {
        send_fault(ESP32_COMM_ERR_SENSOR, "HX711_NOT_READY");
        return 0U;
    }

    ESP32_Comm_SendLegacyStatus("FLOW_START");

    if(outer_door_open_test() == 0U)
    {
        return 0U;
    }

    if(tray_extend_test() == 0U)
    {
        return 0U;
    }

    wait_ret = wait_for_payload(WEIGHT_WAIT_TIMEOUT_MS);
    if(wait_ret == 2U)
    {
        /* 被中止，直接返回，由上层处理后续状态。 */
        return 0U;
    }

    if(wait_ret == 0U)
    {
        /*
         * 超时没等到货物时，也要尽量把机构收回到安全态，
         * 防止门和托盘一直保持外伸状态。
         */
        if(close_to_safe_locked(0U) == 0U)
        {
            return 0U;
        }
        ESP32_Comm_SendLegacyStatus("FLOW_ABORTED");
        send_fault(ESP32_COMM_ERR_TIMEOUT, "WEIGHT_TIMEOUT");
        return 0U;
    }

    /* 正常检测到货物后，执行收口。 */
    if(close_to_safe_locked(1U) == 0U)
    {
        return 0U;
    }

    /*
     * SAFE_LOCKED 后打开内门，表示货物已安全转移到用户可取的一侧。
     * 当前 inner_door 由舵机控制。
     */
    InnerDoor_Open();
    ESP32_Comm_SendLegacyStatus("INNER_OPEN_OK");
    send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_COMPLETE, ESP32_COMM_ERR_NONE);
    ESP32_Comm_SendLegacyStatus("FLOW_DONE");
    return 1U;
}

/*
 * 处理一条“新协议命令”。
 *
 * 注意这里只处理：
 * - pkt->is_proto == 1
 * - pkt->proto_type == CMD
 *
 * 其它协议类型通常是 CH32 自己发送出去的状态帧，不应该作为控制输入。
 */
static void handle_proto_command(const ESP32_Comm_Packet_t *pkt)
{
    if(pkt->proto_type != ESP32_COMM_PROTO_TYPE_CMD)
    {
        return;
    }

    /*
     * 如果当前正忙，则只有以下命令允许插入：
     * - QUERY_STATUS
     * - READ_WEIGHT
     * - ABORT
     *
     * 其他动作命令全部拒绝，避免流程重入。
     */
    if(s_action_busy != 0U)
    {
        switch(pkt->proto_cmd)
        {
            case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
            case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
            case ESP32_COMM_PROTO_CMD_ABORT:
                break;
            default:
                send_nack_current(ESP32_COMM_ERR_BUSY);
                return;
        }
    }

    switch(pkt->proto_cmd)
    {
        case ESP32_COMM_PROTO_CMD_PROBE_READY:
            /* 探测就绪：回复 ACK，并立刻发一份 READY 心跳。 */
            send_ack_current();
            send_idle_heartbeat();
            break;

        case ESP32_COMM_PROTO_CMD_START_DOCK:
            action_begin();
            send_ack_current();
            (void)run_delivery_flow();
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_OPEN_DOOR:
            action_begin();
            send_ack_current();
            (void)outer_door_open_test();
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_CLOSE_DOOR:
            action_begin();
            send_ack_current();
            if(outer_door_close_test() != 0U)
            {
                send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_SAFE_LOCKED, ESP32_COMM_ERR_NONE);
            }
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_EXTEND_TRAY:
            action_begin();
            send_ack_current();
            (void)tray_extend_test();
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_RETRACT_TRAY:
            action_begin();
            send_ack_current();
            (void)tray_retract_test();
            action_end_to_ready();
            break;

        case ESP32_COMM_PROTO_CMD_QUERY_STATUS:
            send_status_for_ctx(&s_rsp);
            break;

        case ESP32_COMM_PROTO_CMD_READ_WEIGHT:
            send_weight_for_ctx(&s_rsp);
            break;

        case ESP32_COMM_PROTO_CMD_ABORT:
            send_ack_current();
            stop_all_action();
            s_action_busy = 0U;
            s_action_rsp_valid = 0U;
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
            ESP32_Comm_SendLegacyStatus("STOP_OK");
            break;

        case ESP32_COMM_PROTO_CMD_RESET_FAULT:
            send_ack_current();
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            s_action_rsp_valid = 0U;
            send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_READY, ESP32_COMM_ERR_NONE);
            ESP32_Comm_SendLegacyStatus("FAULT_RESET_OK");
            break;

        default:
            send_nack_current(ESP32_COMM_ERR_UNKNOWN_CMD);
            break;
    }
}

/*
 * 处理一条“老协议字符命令”。
 *
 * 老协议示例：
 * - @A\n : 启动完整流程
 * - @O\n : 开门
 * - @W\n : 读取重量
 *
 * 这里本质上是新协议逻辑的一套兼容入口。
 */
static void handle_legacy_command(char cmd)
{
    /*
     * busy 期间，不允许新的阻塞类动作再次插入。
     * 但像 I / W / S 这种查询 / 停止类命令仍然可以处理。
     */
    if((s_action_busy != 0U) && is_blocking_legacy_cmd(cmd))
    {
        ESP32_Comm_SendLegacyError("BUSY");
        return;
    }

    switch(cmd)
    {
        case 'P':
            ESP32_Comm_SendLegacyAck('P');
            send_idle_heartbeat();
            break;

        case 'A':
            action_begin();
            ESP32_Comm_SendLegacyAck('A');
            (void)run_delivery_flow();
            action_end_to_ready();
            break;

        case 'O':
            action_begin();
            ESP32_Comm_SendLegacyAck('O');
            (void)outer_door_open_test();
            action_end_to_ready();
            break;

        case 'C':
            action_begin();
            ESP32_Comm_SendLegacyAck('C');
            if(outer_door_close_test() != 0U)
            {
                send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_SAFE_LOCKED, ESP32_COMM_ERR_NONE);
            }
            action_end_to_ready();
            break;

        case 'E':
            action_begin();
            ESP32_Comm_SendLegacyAck('E');
            (void)tray_extend_test();
            action_end_to_ready();
            break;

        case 'R':
            action_begin();
            ESP32_Comm_SendLegacyAck('R');
            (void)tray_retract_test();
            action_end_to_ready();
            break;

        case 'I':
            /*
             * 老协议的查询状态：
             * - ACK
             * - 文本输出阶段名
             * - 二进制状态帧也补发一份，便于新旧系统混用
             */
            ESP32_Comm_SendLegacyAck('I');
            ESP32_Comm_SendLegacyStatus(stage_name(s_current_stage));
            ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_STATUS,
                                      ESP32_COMM_PROTO_CMD_QUERY_STATUS,
                                      0U,
                                      s_current_stage,
                                      s_last_error,
                                      build_flags(),
                                      s_last_weight_g);
            break;

        case 'K':
            ESP32_Comm_SendLegacyAck('K');
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            s_action_rsp_valid = 0U;
            send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_READY, ESP32_COMM_ERR_NONE);
            ESP32_Comm_SendLegacyStatus("FAULT_RESET_OK");
            break;

        case 'W':
            ESP32_Comm_SendLegacyAck('W');
            if(ensure_hx711_ready() == 0U)
            {
                send_fault(ESP32_COMM_ERR_SENSOR, "HX711_NOT_READY");
            }
            else
            {
                char text[48];
                s_last_weight_g = get_weight_avg(3U);
                snprintf(text, sizeof(text), "WEIGHT=%ld", (long)s_last_weight_g);
                ESP32_Comm_SendLegacyStatus(text);
                ESP32_Comm_SendProtoState(ESP32_COMM_PROTO_TYPE_STATUS,
                                          ESP32_COMM_PROTO_CMD_READ_WEIGHT,
                                          0U,
                                          s_current_stage,
                                          ESP32_COMM_ERR_NONE,
                                          build_flags(),
                                          s_last_weight_g);
            }
            break;

        case 'S':
            ESP32_Comm_SendLegacyAck('S');
            stop_all_action();
            ESP32_Comm_FlushRx();
            s_action_busy = 0U;
            s_action_rsp_valid = 0U;
            s_last_error = ESP32_COMM_ERR_NONE;
            s_locked = 0U;
            send_stage(ESP32_COMM_PROTO_TYPE_EVENT, ESP32_COMM_STAGE_IDLE, ESP32_COMM_ERR_NONE);
            ESP32_Comm_SendLegacyStatus("STOP_OK");
            break;

        default:
            ESP32_Comm_SendLegacyError("UNKNOWN_CMD");
            ESP32_Comm_SendProtoNack(ESP32_COMM_PROTO_CMD_NONE, 0U, ESP32_COMM_ERR_UNKNOWN_CMD);
            break;
    }
}

/*
 * CH32 应用层初始化。
 *
 * 调用顺序建议：
 * main.c 中系统时钟 / Delay / printf 初始化完成后，尽快调用本函数。
 *
 * 初始化内容：
 * 1. 初始化通信接口 USART1
 * 2. 初始化所有执行器 / 传感器模块
 * 3. 执行安全默认动作（全部停止、内门关闭）
 * 4. 清空状态变量
 * 5. 发送首个 READY 心跳
 */
void CH32_App_Init(void)
{
    ESP32_Comm_Init();

    TMC2209_Init();
    PushRod_Init();
    HX711_Init();
    InnerDoor_Init();

    /* 上电后先把机构全部收敛到安全初态。 */
    stop_all_action();
    InnerDoor_Close();

    /* 复位所有运行态变量。 */
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

    /* 初始化完成后，立刻告诉 ESP32：CH32 已上线且 READY。 */
    send_idle_heartbeat();
}

/*
 * CH32 应用层单次轮询函数。
 *
 * 这是 main while(1) 中应该反复调用的主入口。
 *
 * 工作逻辑：
 * 1. 如果串口里有完整命令包，就取出来处理
 * 2. 如果暂时没收到命令，就小延时 10ms
 * 3. 系统空闲足够久时，周期性广播 READY 心跳
 *
 * 注意：
 * 这个函数本身不是 RTOS 任务，而是“裸机轮询调度入口”。
 */
void CH32_App_RunOnce(void)
{
    ESP32_Comm_Packet_t pkt;

    if(ESP32_Comm_ReadPacket(&pkt) != 0U)
    {
        /*
         * 收到命令后，说明系统当前不再是“纯空闲”，
         * 所以把空闲计时清零。
         */
        s_idle_ms = 0U;
        rsp_ctx_set_from_packet(&pkt);

        if(pkt.is_proto != 0U)
        {
            handle_proto_command(&pkt);
        }
        else
        {
            handle_legacy_command(pkt.legacy_cmd);
        }
    }
    else
    {
        /*
         * 当前没有收到完整命令包：
         * - 稍微 sleep 一下，降低空转 CPU 占用
         * - 同时累计空闲时长
         */
        Delay_Ms(10);
        s_idle_ms += 10U;

        /*
         * 只有在系统空闲且达到心跳周期时，才发 READY 心跳。
         * 若当前仍在执行动作，则动作内部会自行发送状态事件或心跳。
         */
        if((s_action_busy == 0U) && (s_idle_ms >= READY_BROADCAST_INTERVAL_MS))
        {
            send_idle_heartbeat();
            s_idle_ms = 0U;
        }
    }
}
