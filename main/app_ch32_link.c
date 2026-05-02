/*
 * app_ch32_link.c - ESP32-P4 与 CH32V203 副控通信模块（详细注释版）
 *
 * 这个文件负责主控 ESP32-P4 和副控 CH32V203 之间的串口通信。
 * 链路使用二进制协议帧，包含帧头、版本、序号、命令、载荷长度、payload 和 CRC16。
 *
 * 在 SkyAnchor 项目里，ESP32-P4 负责 UI、视觉、状态机和云端，CH32V203 负责电机、推杆、限位、称重等实时执行。
 * 本文件就是两颗芯片之间的“可靠通信通道”，向上给 app_ctrl.c 提供回调，向下封装 UART 收发和 ACK/READY 等同步机制。
 */

#include "app_ch32_link.h"                         // 项目自定义模块头文件，声明 app_ch32_link 对外提供的接口。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy。
#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/event_groups.h"                 // FreeRTOS 事件组，用 bit 标志表示 READY、ACK、MQTT 连接等状态。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include "driver/gpio.h"                           // GPIO 驱动，串口/握手/控制引脚可能需要配置。
#include "driver/uart.h"                           // ESP-IDF UART 驱动，用于 ESP32-P4 与 CH32V203 串口通信。
#include "esp_check.h"                             // ESP-IDF 错误检查宏，例如 ESP_RETURN_ON_FALSE/ESP_GOTO_ON_ERROR。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
static const char *TAG = "app_ch32_link";                        // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
#define CH32_EVT_READY           BIT0
#define CH32_EVT_ACK             BIT1
#define CH32_EVT_NACK            BIT2
#define APP_CH32_PROTO_OVERHEAD  (9U)                    // 项目级编译期配置宏，用于集中管理参数，避免 magic number 散落在代码里。
#define APP_CH32_PROTO_MIN_FRAME (APP_CH32_PROTO_OVERHEAD) // 最小门限，用于过滤异常数据。
#define APP_CH32_PROTO_MAX_FRAME (APP_CH32_PROTO_OVERHEAD + APP_CH32_LINK_PROTO_MAX_PAYLOAD) // 最大门限，用于限制资源或过滤异常数据。
#ifndef APP_CH32_LINK_READY_STALE_MS
#define APP_CH32_LINK_READY_STALE_MS (3000U)
#endif
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    uart_port_t uart_num;
    EventGroupHandle_t event_group;
    TaskHandle_t rx_task;
    app_ch32_line_cb_t cb;
    void *user_ctx;
    bool inited;
    bool ready;
    int32_t last_weight_g;
    bool has_weight;
    app_ch32_proto_cmd_t last_ack_cmd;
    uint8_t last_ack_seq;
    app_ch32_proto_cmd_t last_nack_cmd;
    uint8_t last_nack_seq;
    uint8_t last_nack_error;
    uint8_t next_seq;
    TickType_t last_rx_tick;
} app_ch32_link_ctx_t;
static app_ch32_link_ctx_t s_ctx = {0};                          // 模块级静态变量 s_ctx，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 计算 CRC16-IBM 校验值，用于二进制协议帧的完整性校验。
 */
static uint16_t app_ch32_crc16_ibm(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
/*
 * 从小端字节序 payload 中读取 32 位有符号整数，例如重量值。
 */
static int32_t app_ch32_read_i32_le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}
/*
 * 从小端字节序 payload 中读取 16 位无符号整数，例如 flags/detail。
 */
static uint16_t app_ch32_read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
/*
 * 把上层保留的单字符命令映射成 CH32 二进制协议命令码。
 */
static app_ch32_proto_cmd_t app_ch32_char_cmd_to_proto(char cmd)
{
    switch (cmd) {
        case 'P': return APP_CH32_PROTO_CMD_PROBE_READY;
        case 'A': return APP_CH32_PROTO_CMD_START_DOCK;
        case 'O': return APP_CH32_PROTO_CMD_OPEN_DOOR;
        case 'C': return APP_CH32_PROTO_CMD_CLOSE_DOOR;
        case 'E': return APP_CH32_PROTO_CMD_EXTEND_TRAY;
        case 'R': return APP_CH32_PROTO_CMD_RETRACT_TRAY;
        case 'I': return APP_CH32_PROTO_CMD_QUERY_STATUS;
        case 'K': return APP_CH32_PROTO_CMD_RESET_FAULT;
        case 'W': return APP_CH32_PROTO_CMD_READ_WEIGHT;
        case 'S': return APP_CH32_PROTO_CMD_ABORT;
        default:  return APP_CH32_PROTO_CMD_NONE;
    }
}
/*
 * 把 CH32 执行阶段枚举转成字符串，方便 UI 和日志显示。
 */
const char *app_ch32_link_proto_stage_name(app_ch32_proto_stage_t stage)
{
    switch (stage) {
        case APP_CH32_STAGE_IDLE:            return "IDLE";
        case APP_CH32_STAGE_READY:           return "READY";
        case APP_CH32_STAGE_DOOR_OPENING:    return "DOOR_OPENING";
        case APP_CH32_STAGE_DOOR_OPENED:     return "DOOR_OPENED";
        case APP_CH32_STAGE_TRAY_EXTENDING:  return "TRAY_EXTENDING";
        case APP_CH32_STAGE_TRAY_EXTENDED:   return "TRAY_EXTENDED";
        case APP_CH32_STAGE_WAITING_CARGO:   return "WAITING_CARGO";
        case APP_CH32_STAGE_CARGO_DETECTED:  return "CARGO_DETECTED";
        case APP_CH32_STAGE_TRAY_RETRACTING: return "TRAY_RETRACTING";
        case APP_CH32_STAGE_TRAY_RETRACTED:  return "TRAY_RETRACTED";
        case APP_CH32_STAGE_DOOR_CLOSING:    return "DOOR_CLOSING";
        case APP_CH32_STAGE_SAFE_LOCKED:     return "SAFE_LOCKED";
        case APP_CH32_STAGE_COMPLETE:        return "COMPLETE";
        case APP_CH32_STAGE_FAULT:           return "FAULT";
        case APP_CH32_STAGE_UNKNOWN:
        default:                             return "UNKNOWN";
    }
}
/*
 * 把 CH32 错误码转成字符串，方便定位超时、限位、堵转、传感器异常等问题。
 */
const char *app_ch32_link_proto_error_name(uint8_t err)
{
    switch ((app_ch32_proto_error_t)err) {
        case APP_CH32_ERR_NONE:        return "NONE";
        case APP_CH32_ERR_TIMEOUT:     return "TIMEOUT";
        case APP_CH32_ERR_LIMIT:       return "LIMIT";
        case APP_CH32_ERR_WEIGHT:      return "WEIGHT";
        case APP_CH32_ERR_JAM:         return "JAM";
        case APP_CH32_ERR_SENSOR:      return "SENSOR";
        case APP_CH32_ERR_SAFETY:      return "SAFETY";
        case APP_CH32_ERR_BUSY:        return "BUSY";
        case APP_CH32_ERR_UNKNOWN_CMD: return "UNKNOWN_CMD";
        case APP_CH32_ERR_BAD_CRC:     return "BAD_CRC";
        case APP_CH32_ERR_INTERNAL:    return "INTERNAL";
        default:                       return "UNKNOWN";
    }
}
/*
 * 根据 CH32 阶段和 flags 判断副控是否处于可接收新命令的 ready 状态。
 */
static bool app_ch32_proto_stage_indicates_ready(app_ch32_proto_stage_t stage, uint16_t flags)
{
    if ((flags & APP_CH32_FLAG_READY) != 0U) {
        return true;
    }
    switch (stage) {
        case APP_CH32_STAGE_IDLE:
        case APP_CH32_STAGE_READY:
        case APP_CH32_STAGE_COMPLETE:
        case APP_CH32_STAGE_SAFE_LOCKED:
            return true;
        default:
            return false;
    }
}
/*
 * ready 不能只看最后一次状态位：如果 CH32 掉线，最后一次 READY 会一直留在内存里。
 * 这里用最近一次有效协议帧时间做保鲜，超过阈值就主动失效。
 */
static bool app_ch32_ready_is_fresh(void)
{
    if (!s_ctx.ready) {
        return false;
    }

    const TickType_t age_ticks = xTaskGetTickCount() - s_ctx.last_rx_tick;
    if (age_ticks > pdMS_TO_TICKS(APP_CH32_LINK_READY_STALE_MS)) {
        s_ctx.ready = false;
        if (s_ctx.event_group != NULL) {
            xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);
        }
        return false;
    }
    return true;
}
/*
 * 处理所有消息都会影响的公共状态，例如 ready 标志、最后重量、ACK 事件等。
 */
static void app_ch32_apply_common_side_effects(const app_ch32_line_t *msg)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (msg == NULL) {
        return;
    }
    s_ctx.last_rx_tick = xTaskGetTickCount();
    if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
        (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
        (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
        (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        if (msg->payload_len >= 8U) {
            s_ctx.last_weight_g = msg->proto_weight_g;
            s_ctx.has_weight = true;
            s_ctx.ready = app_ch32_proto_stage_indicates_ready(msg->proto_stage, msg->proto_flags);
            if (s_ctx.ready) {
                // 置位事件组 bit，用来通知其他任务某个事件已经发生。
                xEventGroupSetBits(s_ctx.event_group, CH32_EVT_READY);
            } else {
                xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);
            }
        }
    }
    if (msg->type == APP_CH32_LINE_PROTO_ACK) {
        s_ctx.last_ack_cmd = (app_ch32_proto_cmd_t)msg->proto_cmd;
        s_ctx.last_ack_seq = msg->proto_seq;
        // 置位事件组 bit，用来通知其他任务某个事件已经发生。
        xEventGroupSetBits(s_ctx.event_group, CH32_EVT_ACK);
    }
    if (msg->type == APP_CH32_LINE_PROTO_NACK) {
        s_ctx.last_nack_cmd = (app_ch32_proto_cmd_t)msg->proto_cmd;
        s_ctx.last_nack_seq = msg->proto_seq;
        s_ctx.last_nack_error = msg->proto_detail;
        xEventGroupSetBits(s_ctx.event_group, CH32_EVT_NACK);
    }
}
/*
 * 把解析后的结构化消息回调给上层 app_ctrl.c。
 */
static void app_ch32_dispatch_msg(const app_ch32_line_t *msg)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (msg == NULL) {
        return;
    }
    app_ch32_apply_common_side_effects(msg);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_ctx.cb != NULL) {
        s_ctx.cb(msg, s_ctx.user_ctx);
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "CH32 <=proto=> %s", msg->line);
}
/*
 * 解析新版二进制协议帧，检查帧头、长度、CRC，并提取 stage/weight/flags 等字段。
 */
static bool app_ch32_parse_proto_frame(const uint8_t *frame, size_t frame_len, app_ch32_line_t *out)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if ((frame == NULL) || (out == NULL)) {
        return false;
    }

    /*
     * 协议帧最短也要包含：
     * SOF1、SOF2、VER、TYPE、CMD、SEQ、LEN、CRC16_LO、CRC16_HI。
     * 如果连最小长度都不够，就一定不是合法协议帧。
     */
    if (frame_len < APP_CH32_PROTO_MIN_FRAME) {
        return false;
    }

    /*
     * 检查帧头。
     *
     * 0x55 0xAA 是新版二进制协议的同步头。
     * RX 任务会用第一个字节进入 proto_active 状态，这里再做一次完整校验，
     * 避免噪声被误解析成协议帧。
     */
    if ((frame[0] != APP_CH32_PROTO_SOF1) || (frame[1] != APP_CH32_PROTO_SOF2)) {
        return false;
    }

    /*
     * 检查协议版本。
     *
     * 当前 ESP32-P4 端只理解 APP_CH32_PROTO_VER 指定的版本。
     * 如果后续 CH32 升级协议版本，可以在这里加入兼容分支。
     */
    if (frame[2] != APP_CH32_PROTO_VER) {
        return false;
    }

    /*
     * LEN 位于第 6 字节，表示 payload 的字节数。
     * 总帧长 = 固定开销 9 字节 + payload_len。
     */
    const uint8_t payload_len = frame[6];
    if ((size_t)(APP_CH32_PROTO_OVERHEAD + payload_len) != frame_len) {
        return false;
    }

    /*
     * CRC16 覆盖范围从 SOF1 开始，到 payload 结束。
     * 这和 CH32 端 esp32_comm_crc16_ibm(frame, frame_len - 2) 的算法保持一致。
     */
    const uint16_t crc_expect = app_ch32_read_u16_le(&frame[7 + payload_len]);
    const uint16_t crc_actual = app_ch32_crc16_ibm(frame, (size_t)(7U + payload_len));
    if (crc_expect != crc_actual) {
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "proto crc mismatch, expect=0x%04x actual=0x%04x", crc_expect, crc_actual);
        return false;
    }

    /*
     * CRC 通过后再清空输出结构体。
     *
     * 这样调用者只会拿到“已经确认合法”的结构化消息，
     * 不会因为半帧或坏帧污染上层状态机。
     */
    memset(out, 0, sizeof(*out));
    out->proto_type = frame[3];    // TYPE：ACK/NACK/STATUS/EVENT/ERROR/HEARTBEAT 等消息类型。
    out->proto_cmd = frame[4];     // CMD：这条 ACK/NACK 对应的命令，或事件关联的命令。
    out->proto_seq = frame[5];     // SEQ：帧序号，便于后续扩展更严格的请求/响应匹配。
    out->payload_len = payload_len; // 保存 payload 长度，供上层或调试代码判断 payload 是否完整。

    /*
     * 复制 payload。
     *
     * payload 最长由 APP_CH32_LINK_PROTO_MAX_PAYLOAD 限制，
     * 所以这里拷贝到 app_ch32_line_t 内部固定数组是安全的。
     */
    if (payload_len > 0U) {
        memcpy(out->payload, &frame[7], payload_len);
    }

    /*
     * 根据 TYPE 把协议消息映射到 app_ch32_line_type_t。
     *
     * 统一后的 type 会被 app_ctrl_on_ch32_line() 使用，
     * 这样上层不需要直接关心协议原始数字。
     */
    switch ((app_ch32_proto_type_t)out->proto_type) {
        case APP_CH32_PROTO_TYPE_ACK:
            out->type = APP_CH32_LINE_PROTO_ACK;
            snprintf(out->line, sizeof(out->line),
                     "PROTO ACK cmd=0x%02X seq=%u",
                     out->proto_cmd,
                     (unsigned)out->proto_seq);
            break;
        case APP_CH32_PROTO_TYPE_NACK:
            out->type = APP_CH32_LINE_PROTO_NACK;
            out->proto_detail = (payload_len >= 1U) ? out->payload[0] : APP_CH32_ERR_INTERNAL;
            snprintf(out->line, sizeof(out->line),
                     "PROTO NACK cmd=0x%02X seq=%u err=%s",
                     out->proto_cmd,
                     (unsigned)out->proto_seq,
                     app_ch32_link_proto_error_name(out->proto_detail));
            break;
        case APP_CH32_PROTO_TYPE_STATUS:
            out->type = APP_CH32_LINE_PROTO_STATUS;
            break;
        case APP_CH32_PROTO_TYPE_EVENT:
            out->type = APP_CH32_LINE_PROTO_EVENT;
            break;
        case APP_CH32_PROTO_TYPE_ERROR:
            out->type = APP_CH32_LINE_PROTO_ERROR;
            break;
        case APP_CH32_PROTO_TYPE_HEARTBEAT:
            out->type = APP_CH32_LINE_PROTO_HEARTBEAT;
            break;
        default:
            out->type = APP_CH32_LINE_UNKNOWN;
            break;
    }

    /*
     * 状态类消息的 payload 约定：
     * byte 0      : stage，表示 CH32 当前执行阶段；
     * byte 1      : detail，事件细节或错误码；
     * byte 2..3   : flags，小端，表示 ready/busy/限位/货物检测等 bit；
     * byte 4..7   : weight_g，小端有符号整数，表示称重克数。
     */
    if ((out->type == APP_CH32_LINE_PROTO_STATUS) ||
        (out->type == APP_CH32_LINE_PROTO_EVENT) ||
        (out->type == APP_CH32_LINE_PROTO_ERROR) ||
        (out->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        if (payload_len >= 8U) {
            out->proto_stage = (app_ch32_proto_stage_t)out->payload[0]; // CH32 机械流程阶段，例如开门、托盘伸出、等待货物。
            out->proto_detail = out->payload[1];                        // 状态细节；错误帧中这里通常是错误码。
            out->proto_flags = app_ch32_read_u16_le(&out->payload[2]);   // flags 使用小端读取，兼容 CH32 端 C 结构体打包。
            out->proto_weight_g = app_ch32_read_i32_le(&out->payload[4]); // 最近一次称重值，单位 g。
        }

        /*
         * 生成一行可读调试文本。
         *
         * 这样串口日志和 UI 调试文本可以共用同一个 line 字段，
         * 上层调试时不用手工解码二进制 payload。
         */
        if (out->type == APP_CH32_LINE_PROTO_ERROR) {
            snprintf(out->line, sizeof(out->line),
                     "PROTO ERR stage=%s err=%s flags=0x%04X w=%ldg",
                     app_ch32_link_proto_stage_name(out->proto_stage),
                     app_ch32_link_proto_error_name(out->proto_detail),
                     (unsigned)out->proto_flags,
                     (long)out->proto_weight_g);
        } else {
            snprintf(out->line, sizeof(out->line),
                     "PROTO %s stage=%s detail=%u flags=0x%04X w=%ldg",
                     (out->type == APP_CH32_LINE_PROTO_STATUS) ? "STS" :
                     (out->type == APP_CH32_LINE_PROTO_EVENT) ? "EVT" : "HB",
                     app_ch32_link_proto_stage_name(out->proto_stage),
                     (unsigned)out->proto_detail,
                     (unsigned)out->proto_flags,
                     (long)out->proto_weight_g);
        }
    }
    return true;
}
/*
 * UART 接收任务，持续读取并解析 CH32 二进制协议帧。
 */
static void app_ch32_link_rx_task(void *arg)
{
    uint8_t ch = 0;
    uint8_t proto_buf[APP_CH32_PROTO_MAX_FRAME] = {0};
    size_t proto_len = 0;
    size_t proto_expect = 0;

    /*
     * xTaskCreate() 要求任务函数必须接收 void *arg。
     * 本任务不需要外部参数，所以显式丢弃，避免编译器告警。
     */
    (void)arg;

    while (1) {
        // 从 UART 接收缓冲中读取 CH32 发来的数据。
        int len = uart_read_bytes(s_ctx.uart_num, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        /*
         * 空闲状态只等待 SOF1。
         * 其他噪声字节直接丢弃，直到重新同步到协议帧头。
         */
        if (proto_len == 0U) {
            if (ch != APP_CH32_PROTO_SOF1) {
                continue;
            }
            proto_buf[proto_len++] = ch;
            proto_expect = 0;
            continue;
        }

        /*
         * 第二字节必须是 SOF2。
         * 如果又收到 SOF1，就把它当作新帧头继续等待 SOF2。
         */
        if (proto_len == 1U) {
            if (ch == APP_CH32_PROTO_SOF2) {
                proto_buf[proto_len++] = ch;
                continue;
            }
            proto_len = (ch == APP_CH32_PROTO_SOF1) ? 1U : 0U;
            proto_buf[0] = (proto_len == 1U) ? ch : 0U;
            proto_expect = 0;
            continue;
        }

        /*
         * 把当前字节追加到协议缓冲。
         * 如果超过缓冲上限，说明这帧异常，直接丢弃并重新等待下一帧。
         */
        if (proto_len < sizeof(proto_buf)) {
            proto_buf[proto_len++] = ch;
        } else {
            proto_len = 0;
            proto_expect = 0;
            continue;
        }

        /*
         * 收到前 7 个字节后，LEN 字段已经到位。
         * 此时就能算出完整帧长度，后面只需要继续等到 proto_len == proto_expect。
         */
        if (proto_len == 7U) {
            const uint8_t payload_len = proto_buf[6];
            if (payload_len > APP_CH32_LINK_PROTO_MAX_PAYLOAD) {
                proto_len = 0;
                proto_expect = 0;
                // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
                ESP_LOGW(TAG, "proto payload too large: %u", (unsigned)payload_len);
                continue;
            }
            proto_expect = APP_CH32_PROTO_OVERHEAD + payload_len;
        }

        /*
         * 完整帧已经收齐。
         * 解析成功才派发给上层，否则丢弃坏帧并等待下一帧。
         */
        if ((proto_expect >= APP_CH32_PROTO_MIN_FRAME) && (proto_len == proto_expect)) {
            app_ch32_line_t msg = {0};
            if (app_ch32_parse_proto_frame(proto_buf, proto_len, &msg)) {
                app_ch32_dispatch_msg(&msg);
            }
            proto_len = 0;
            proto_expect = 0;
        }
    }
}
/*
 * 发送前清理/准备 UART 发送状态，减少上一帧残留影响。
 */
static esp_err_t app_ch32_link_prepare_tx_idle(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << APP_CH32_LINK_TX_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config tx idle failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_CH32_LINK_TX_GPIO, 1), TAG, "gpio_set_level tx idle failed");
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 按二进制协议发送命令帧。
 *
 * 协议帧包含版本号、命令码、序号、payload 长度和 CRC16，
 * 用来承载结构化状态和后续扩展字段。
 */
esp_err_t app_ch32_link_send_proto(app_ch32_proto_cmd_t cmd,
                                   const void *payload,
                                   uint8_t payload_len,
                                   uint8_t *out_seq)
{
    /*
     * 发送前先确认模块已经初始化。
     * 如果 UART 驱动和事件组还没准备好，直接写串口会失败或造成状态不同步。
     */
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    /*
     * 限制 payload 长度，保证 frame[] 和 app_ch32_line_t.payload[] 都不会越界。
     */
    ESP_RETURN_ON_FALSE(payload_len <= APP_CH32_LINK_PROTO_MAX_PAYLOAD,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "payload too large");
    ESP_RETURN_ON_FALSE((payload_len == 0U) || (payload != NULL),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "payload is null");

    /*
     * frame[] 是即将写入 UART 的完整二进制帧。
     * idx 始终指向下一个待写入位置，用连续自增保证字段顺序和协议定义一致。
     */
    uint8_t frame[APP_CH32_PROTO_MAX_FRAME] = {0};

    /*
     * 每发送一帧命令，序号自增。
     * 当前实现会用它做 ACK/NACK 请求-响应匹配，避免旧 ACK 误确认新命令。
     */
    const uint8_t seq = ++s_ctx.next_seq;
    size_t idx = 0;

    frame[idx++] = APP_CH32_PROTO_SOF1;              // 帧头第 1 字节，用于接收端同步。
    frame[idx++] = APP_CH32_PROTO_SOF2;              // 帧头第 2 字节，和 SOF1 一起降低误判概率。
    frame[idx++] = APP_CH32_PROTO_VER;               // 协议版本号，便于后续兼容升级。
    frame[idx++] = (uint8_t)APP_CH32_PROTO_TYPE_CMD; // TYPE=CMD，表示 ESP32 正在向 CH32 下发命令。
    frame[idx++] = (uint8_t)cmd;                     // CMD 命令码，例如 START_DOCK、OPEN_DOOR、ABORT。
    frame[idx++] = seq;                              // SEQ 帧序号。
    frame[idx++] = payload_len;                      // LEN payload 字节数。

    /*
     * 可选 payload。
     *
     * 当前大多数控制命令没有 payload，所以调用时常传 NULL + 0。
     * 后续如果要下发速度、阈值、校准参数，就可以放在 payload 里。
     */
    if ((payload_len > 0U) && (payload != NULL)) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }

    /*
     * 计算并追加 CRC16。
     *
     * CRC 覆盖 SOF1 到 payload 末尾，并使用小端写入。
     * CH32 端发送/接收新协议帧也是这个范围。
     */
    const uint16_t crc = app_ch32_crc16_ibm(frame, (size_t)(7U + payload_len));
    frame[idx++] = (uint8_t)(crc & 0xFFU);          // CRC 低字节。
    frame[idx++] = (uint8_t)((crc >> 8) & 0xFFU);   // CRC 高字节。

    // 通过 UART 向 CH32 发送命令或协议帧。
    int written = uart_write_bytes(s_ctx.uart_num, (const char *)frame, idx);
    ESP_RETURN_ON_FALSE(written == (int)idx, ESP_FAIL, TAG, "uart_write_bytes proto failed");

    /*
     * 如果调用者关心本次发送的序号，就通过 out_seq 带回去。
     * 当前等待 ACK 的逻辑还没有强制使用 seq，但保留这个接口方便后续增强。
     */
    if (out_seq != NULL) {
        *out_seq = seq;
    }

    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "ESP32 => CH32 proto: cmd=0x%02X seq=%u len=%u",
             (unsigned)cmd,
             (unsigned)seq,
             (unsigned)payload_len);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 等待指定协议命令的 ACK，用事件组实现同步等待和超时保护。
 */
static esp_err_t app_ch32_link_wait_ack_for_cmd(app_ch32_proto_cmd_t cmd, uint8_t seq, uint32_t timeout_ms)
{
    /*
     * elapsed_ms 记录已经等待了多久。
     * 这里不用一次性等 timeout_ms，而是分片等待，方便中途处理“收到了 ACK 但命令不匹配”的情况。
     */
    uint32_t elapsed_ms = 0;

    /*
     * slice_ms 是每次等待事件组的时间片。
     * 如果宏配置为 0，就回退到 50ms，避免 pdMS_TO_TICKS(0) 变成立刻返回导致忙等。
     */
    uint32_t slice_ms = APP_CH32_LINK_ACK_POLL_MS;
    if (slice_ms == 0U) {
        slice_ms = 50U;
    }

    /*
     * 循环等待 ACK，直到：
         * - 收到目标协议命令和 seq 对应的 ACK，返回 ESP_OK；
         * - 收到目标协议命令和 seq 对应的 NACK，立即返回失败；
         * - 超过 timeout_ms，返回 ESP_ERR_TIMEOUT。
         */
    while (elapsed_ms < timeout_ms) {
        /*
         * 最后一轮等待不能超过总超时。
         * 例如 timeout=120ms，slice=50ms，则等待 50 + 50 + 20。
         */
        uint32_t this_wait = slice_ms;
        if ((elapsed_ms + this_wait) > timeout_ms) {
            this_wait = timeout_ms - elapsed_ms;
        }

        /*
         * 等待 CH32_EVT_ACK。
         *
         * clear_on_exit=pdTRUE 表示等到 ACK 后自动清除 bit，
         * 防止同一个 ACK 被下一轮等待重复消费。
         */
        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                               CH32_EVT_ACK | CH32_EVT_NACK,
                                               pdTRUE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(this_wait));
        elapsed_ms += this_wait;

        /*
         * 这一片时间里没有 ACK，就继续等下一片。
         */
        if ((bits & (CH32_EVT_ACK | CH32_EVT_NACK)) == 0U) {
            continue;
        }

        /*
         * 收到了 ACK，还要确认 ACK 对应的是当前等待的协议命令和序号。
         */
        if (((bits & CH32_EVT_ACK) != 0U) &&
            (s_ctx.last_ack_cmd == cmd) &&
            (s_ctx.last_ack_seq == seq)) {
            // 正常返回 ESP_OK，表示该步骤执行成功。
            return ESP_OK;
        }

        if (((bits & CH32_EVT_NACK) != 0U) &&
            (s_ctx.last_nack_cmd == cmd) &&
            (s_ctx.last_nack_seq == seq)) {
            ESP_LOGW(TAG,
                     "CH32 rejected cmd=0x%02X seq=%u err=%s",
                     (unsigned)cmd,
                     (unsigned)seq,
                     app_ch32_link_proto_error_name(s_ctx.last_nack_error));
            s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
            return ESP_ERR_INVALID_RESPONSE;
        }

        if ((bits & CH32_EVT_ACK) != 0U) {
            ESP_LOGW(TAG, "ack mismatch, expect=0x%02X/%u actual=0x%02X/%u",
                     (unsigned)cmd,
                     (unsigned)seq,
                     (unsigned)s_ctx.last_ack_cmd,
                     (unsigned)s_ctx.last_ack_seq);
            s_ctx.last_ack_cmd = APP_CH32_PROTO_CMD_NONE; // 清掉不匹配 ACK，继续等待目标命令的 ACK。
        }
        if ((bits & CH32_EVT_NACK) != 0U) {
            ESP_LOGW(TAG, "nack mismatch, expect=0x%02X/%u actual=0x%02X/%u err=%s",
                     (unsigned)cmd,
                     (unsigned)seq,
                     (unsigned)s_ctx.last_nack_cmd,
                     (unsigned)s_ctx.last_nack_seq,
                     app_ch32_link_proto_error_name(s_ctx.last_nack_error));
            s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
        }
    }

    /*
     * 超时仍未收到目标命令 ACK。
     * 上层会根据这个结果决定是否回退旧协议、重试或进入故障提示。
     */
    return ESP_ERR_TIMEOUT;
}
/*
 * 初始化 UART、事件组和接收任务，建立 ESP32-P4 与 CH32 的通信通道。
 */
esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx)
{
    /*
     * 防止重复初始化。
     *
     * UART 驱动、事件组和接收任务都属于系统资源，重复安装可能导致：
     * - 同一个 UART 端口被安装两次；
     * - 创建多个 RX 任务同时读同一个串口；
     * - ACK/READY 事件被不同任务抢走，导致状态机误判。
     *
     * 所以如果 s_ctx.inited 已经为 true，就直接返回 ESP_ERR_INVALID_STATE。
     */
    ESP_RETURN_ON_FALSE(!s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    /*
     * 初始化通信上下文。
     *
     * s_ctx 是 app_ch32_link.c 的模块级运行时状态，后续 RX 任务、发送函数、
     * ACK 等待函数都会读取或修改这里的字段。
     */
    s_ctx.uart_num = APP_CH32_LINK_UART_PORT;  // 选择使用哪个 UART 外设端口，默认来自 app_ch32_link.h 中的宏配置。
    s_ctx.cb = cb;                             // 保存上层回调。收到 CH32 消息后，会通过它通知 app_ctrl.c。
    s_ctx.user_ctx = user_ctx;                 // 保存上层传入的用户上下文指针，本工程当前通常传 NULL。

    /*
     * 清空运行期状态。
     *
     * ready / has_weight 都必须从 false 开始，
     * 避免复位或重新初始化时沿用上一轮通信留下的旧状态。
     */
    s_ctx.ready = false;       // 还没有收到 CH32_READY 或协议状态帧，所以暂时认为副控未就绪。
    s_ctx.has_weight = false;  // 尚未收到称重数据，app_ch32_link_last_weight() 不应返回旧重量。
    s_ctx.last_ack_cmd = APP_CH32_PROTO_CMD_NONE; // 清空最近 ACK 对应的协议命令，避免旧 ACK 影响下一次等待。
    s_ctx.last_ack_seq = 0;
    s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
    s_ctx.last_nack_seq = 0;
    s_ctx.last_nack_error = APP_CH32_ERR_NONE;
    s_ctx.next_seq = 0;        // 新版协议帧序号从 0 开始，发送时先自增再写入帧头。
    s_ctx.last_rx_tick = 0;

    /*
     * UART 参数配置。
     *
     * 这里的配置必须和 CH32V203 固件端保持一致，否则两边虽然 TX/RX 连线正确，
     * 但实际收到的数据会乱码或完全收不到。
     */
    uart_config_t uart_cfg = {
        .baud_rate = APP_CH32_LINK_BAUD_RATE,     // 波特率，默认 115200，需要与 CH32 串口初始化一致。
        .data_bits = UART_DATA_8_BITS,            // 8 位数据位，是最常见的 8N1 串口格式之一。
        .parity = UART_PARITY_DISABLE,            // 不使用奇偶校验，对应 8N1 里的 N。
        .stop_bits = UART_STOP_BITS_1,            // 1 位停止位，对应 8N1 里的 1。
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,    // 不启用 RTS/CTS 硬件流控，本链路只使用 TX/RX。
        .source_clk = UART_SCLK_DEFAULT,          // 使用 ESP-IDF 默认 UART 时钟源，保持配置简单。
    };

    /*
     * 在真正安装 UART 驱动前，先把 TX 引脚配置成空闲高电平。
     *
     * UART 空闲态本来就是高电平。提前拉高可以减少上电/初始化瞬间的毛刺，
     * 避免 CH32 把异常低脉冲误判成半个起始位或无效命令。
     */
    ESP_RETURN_ON_ERROR(app_ch32_link_prepare_tx_idle(), TAG, "prepare tx idle failed");

    /*
     * 安装 UART 驱动。
     *
     * APP_CH32_LINK_RX_BUF_SIZE 是底层接收环形缓冲大小。
     * 这里 TX buffer 传 0，表示发送路径不额外创建驱动发送缓冲，
     * uart_write_bytes() 会把当前命令帧直接交给驱动发送。
     *
     * 本模块没有使用 ESP-IDF 的 UART event queue，所以 queue_size 和 queue_handle 都传 0/NULL。
     */
    ESP_RETURN_ON_ERROR(uart_driver_install(s_ctx.uart_num, APP_CH32_LINK_RX_BUF_SIZE, 0, 0, NULL, 0),
                        TAG,
                        "uart_driver_install failed");

    /*
     * 把刚才准备好的 uart_cfg 写入 UART 外设。
     *
     * 如果这里失败，说明端口号或底层驱动状态异常，继续启动 RX 任务没有意义。
     */
    ESP_RETURN_ON_ERROR(uart_param_config(s_ctx.uart_num, &uart_cfg), TAG, "uart_param_config failed");

    /*
     * 设置 UART 引脚映射。
     *
     * ESP32-P4 的 UART 外设可以通过 GPIO matrix 映射到不同管脚。
     * 这里把 TX/RX 映射到项目配置的 CH32 通信管脚，RTS/CTS 不使用，所以传 UART_PIN_NO_CHANGE。
     */
    ESP_RETURN_ON_ERROR(uart_set_pin(s_ctx.uart_num,
                                     APP_CH32_LINK_TX_GPIO,
                                     APP_CH32_LINK_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart_set_pin failed");

    /*
     * 给 RX 引脚打开上拉。
     *
     * 当 CH32 还没有上电、复位中或 TX 引脚暂时高阻时，上拉可以让 ESP32 侧看到稳定高电平，
     * 降低串口接收误触发的概率。
     */
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(APP_CH32_LINK_RX_GPIO, GPIO_PULLUP_ONLY),
                        TAG,
                        "set rx pull-up failed");

    /*
     * 创建事件组。
     *
     * CH32_EVT_READY：副控已经 ready；
     * CH32_EVT_ACK：副控确认收到某条命令。
     *
     * 事件组适合这种“一个后台 RX 任务置位，另一个控制流程等待”的轻量同步。
     */
    s_ctx.event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_ctx.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group create failed");

    /*
     * 创建串口接收任务。
     *
     * app_ch32_link_rx_task() 会常驻后台读取 UART 字节流，
     * 并按二进制协议帧格式完成同步、校验和派发。
     *
     * 栈大小 4096 字节足够容纳行缓冲、协议帧缓冲和解析临时变量；
     * 优先级 8 略高于普通业务任务，避免串口数据积压。
     */
    BaseType_t ok = xTaskCreate(app_ch32_link_rx_task, "ch32_rx", 4096, NULL, 8, &s_ctx.rx_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task create failed");

    /*
     * 所有资源都创建成功后，最后再标记为已初始化。
     *
     * 这样可以避免中途失败时，其他接口误以为通信链路已经可用。
     */
    s_ctx.inited = true;

    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "uart%d init ok, tx=%d rx=%d baud=%d",
             s_ctx.uart_num,
             APP_CH32_LINK_TX_GPIO,
             APP_CH32_LINK_RX_GPIO,
             APP_CH32_LINK_BAUD_RATE);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 发送命令并等待 ACK，适合需要确认副控已收到命令的关键动作。
 */
esp_err_t app_ch32_link_send_cmd_and_wait_ack(char cmd, uint32_t timeout_ms)
{
    /*
     * 这个接口用于关键动作，例如开始接驳、开门、托盘伸出等。
     * 这些命令不能只“发出去就算了”，必须等 CH32 ACK 确认已经收到。
     */
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    const app_ch32_proto_cmd_t proto_cmd = app_ch32_char_cmd_to_proto(cmd);
    ESP_RETURN_ON_FALSE(proto_cmd != APP_CH32_PROTO_CMD_NONE,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "unknown ch32 cmd: %c",
                        cmd);

    // 清除事件组 bit，避免旧事件影响下一次等待。
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK | CH32_EVT_NACK);
    s_ctx.last_ack_cmd = APP_CH32_PROTO_CMD_NONE; // 同步清掉最近 ACK 命令，确保这次等待只认新 ACK。
    s_ctx.last_ack_seq = 0;
    s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
    s_ctx.last_nack_seq = 0;
    s_ctx.last_nack_error = APP_CH32_ERR_NONE;

    uint8_t seq = 0;
    ESP_RETURN_ON_ERROR(app_ch32_link_send_proto(proto_cmd, NULL, 0, &seq), TAG, "proto send failed");
    return app_ch32_link_wait_ack_for_cmd(proto_cmd, seq, timeout_ms);
}
/*
 * 主动探测 CH32 是否在线且 ready，常用于系统启动阶段。
 */
esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms)
{
    /*
     * 探测 ready 也必须在 UART 初始化后进行。
     */
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    /*
     * 如果之前已经收到过 ready 状态，就不用再发探测命令。
     */
    if (app_ch32_ready_is_fresh()) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    s_ctx.ready = false;

    // 清除事件组 bit，避免旧事件影响下一次等待。
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);

    ESP_RETURN_ON_ERROR(app_ch32_link_send_proto(APP_CH32_PROTO_CMD_PROBE_READY, NULL, 0, NULL),
                        TAG,
                        "proto probe send failed");

    // 等待 CH32_EVT_READY，超时后返回 ESP_ERR_TIMEOUT。
    EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                           CH32_EVT_READY,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return ((bits & CH32_EVT_READY) != 0U) ? ESP_OK : ESP_ERR_TIMEOUT;
}
/*
 * 查询最近记录的 CH32 ready 状态。
 */
bool app_ch32_link_is_ready(void)
{
    return app_ch32_ready_is_fresh();
}
/*
 * 读取最近一次 CH32 回传的重量值，给任务状态或 UI 使用。
 */
bool app_ch32_link_last_weight(int32_t *out_weight_g)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if ((!s_ctx.has_weight) || (out_weight_g == NULL)) {
        return false;
    }
    *out_weight_g = s_ctx.last_weight_g;
    return true;
}
