/*
 * app_ch32_link.c - ESP32-P4 与 CH32V203 副控通信模块（详细注释版）
 *
 * 这个文件负责主控 ESP32-P4 和副控 CH32V203 之间的串口通信。
 * 它同时兼容两种通信形式：
 * 1. 旧版文本协议，例如 [ACK]、[STS]、[ERR] 这种可读字符串；
 * 2. 新版二进制协议帧，包含帧头、版本、序号、命令、载荷长度、payload 和 CRC16。
 *
 * 在 SkyAnchor 项目里，ESP32-P4 负责 UI、视觉、状态机和云端，CH32V203 负责电机、推杆、限位、称重等实时执行。
 * 本文件就是两颗芯片之间的“可靠通信通道”，向上给 app_ctrl.c 提供回调，向下封装 UART 收发和 ACK/READY 等同步机制。
 */

#include "app_ch32_link.h"                         // 项目自定义模块头文件，声明 app_ch32_link 对外提供的接口。
#include <ctype.h>                                 // 字符判断函数，例如 isprint，用来过滤串口文本字符。
#include <stdio.h>                                 // C 标准输入输出库，主要用于 snprintf/printf 这类格式化字符串操作。
#include <stdlib.h>                                // 标准库函数，例如 strtol、atoi、内存/数值转换等。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
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
#define APP_CH32_PROTO_OVERHEAD  (9U)                    // 项目级编译期配置宏，用于集中管理参数，避免 magic number 散落在代码里。
#define APP_CH32_PROTO_MIN_FRAME (APP_CH32_PROTO_OVERHEAD) // 最小门限，用于过滤异常数据。
#define APP_CH32_PROTO_MAX_FRAME (APP_CH32_PROTO_OVERHEAD + APP_CH32_LINK_PROTO_MAX_PAYLOAD) // 最大门限，用于限制资源或过滤异常数据。
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
    bool proto_online;
    int32_t last_weight_g;
    bool has_weight;
    char last_ack_cmd;
    uint8_t next_seq;
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
 * 把旧版单字符命令映射成新版协议命令码，便于新旧协议兼容。
 */
static app_ch32_proto_cmd_t app_ch32_legacy_cmd_to_proto(char cmd)
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
 * 把新版协议命令码映射回旧版字符命令，兼容还未升级的 CH32 固件。
 */
static char app_ch32_proto_cmd_to_legacy(app_ch32_proto_cmd_t cmd)
{
    switch (cmd) {
        case APP_CH32_PROTO_CMD_PROBE_READY:  return 'P';
        case APP_CH32_PROTO_CMD_START_DOCK:   return 'A';
        case APP_CH32_PROTO_CMD_OPEN_DOOR:    return 'O';
        case APP_CH32_PROTO_CMD_CLOSE_DOOR:   return 'C';
        case APP_CH32_PROTO_CMD_EXTEND_TRAY:  return 'E';
        case APP_CH32_PROTO_CMD_RETRACT_TRAY: return 'R';
        case APP_CH32_PROTO_CMD_QUERY_STATUS: return 'I';
        case APP_CH32_PROTO_CMD_RESET_FAULT:  return 'K';
        case APP_CH32_PROTO_CMD_READ_WEIGHT:  return 'W';
        case APP_CH32_PROTO_CMD_ABORT:        return 'S';
        default:                              return 0;
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
 * 判断旧版文本行属于 ACK、状态、错误还是调试信息。
 */
static app_ch32_line_type_t app_ch32_classify_legacy_line(const char *line)
{
    if (strncmp(line, "[ACK]", 5) == 0) {
        return APP_CH32_LINE_ACK;
    }
    if (strncmp(line, "[STS]", 5) == 0) {
        return APP_CH32_LINE_STATUS;
    }
    if (strncmp(line, "[ERR]", 5) == 0) {
        return APP_CH32_LINE_ERROR;
    }
    if (strncmp(line, "[DBG]", 5) == 0) {
        return APP_CH32_LINE_DEBUG;
    }
    return APP_CH32_LINE_UNKNOWN;
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
 * 处理所有消息都会影响的公共状态，例如 ready 标志、最后重量、ACK 事件等。
 */
static void app_ch32_apply_common_side_effects(const app_ch32_line_t *msg)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (msg == NULL) {
        return;
    }
    if (msg->is_proto) {
        s_ctx.proto_online = true;
        if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
            (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
            (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
            (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
            if (msg->payload_len >= 8U) {
                s_ctx.last_weight_g = msg->proto_weight_g;
                s_ctx.has_weight = true;
            }
            if (app_ch32_proto_stage_indicates_ready(msg->proto_stage, msg->proto_flags)) {
                s_ctx.ready = true;
                // 置位事件组 bit，用来通知其他任务某个事件已经发生。
                xEventGroupSetBits(s_ctx.event_group, CH32_EVT_READY);
            }
        }
        if (msg->type == APP_CH32_LINE_PROTO_ACK) {
            s_ctx.last_ack_cmd = app_ch32_proto_cmd_to_legacy((app_ch32_proto_cmd_t)msg->proto_cmd);
            // 置位事件组 bit，用来通知其他任务某个事件已经发生。
            xEventGroupSetBits(s_ctx.event_group, CH32_EVT_ACK);
        }
        return;
    }
    if (msg->type == APP_CH32_LINE_STATUS) {
        // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
        if (strstr(msg->line, "CH32_READY") != NULL) {
            s_ctx.ready = true;
            // 置位事件组 bit，用来通知其他任务某个事件已经发生。
            xEventGroupSetBits(s_ctx.event_group, CH32_EVT_READY);
        }
        const char *w = strstr(msg->line, "WEIGHT=");
        // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
        if (w != NULL) {
            s_ctx.last_weight_g = (int32_t)strtol(w + 7, NULL, 10);
            s_ctx.has_weight = true;
        }
    } else if (msg->type == APP_CH32_LINE_ACK) {
        const char *p = msg->line + 5;
        while (*p == ' ') {
            p++;
        }
        s_ctx.last_ack_cmd = *p;
        // 置位事件组 bit，用来通知其他任务某个事件已经发生。
        xEventGroupSetBits(s_ctx.event_group, CH32_EVT_ACK);
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
    if (msg->is_proto) {
        // 信息日志：用于确认程序执行到了哪个阶段。
        ESP_LOGI(TAG, "CH32 <=proto=> %s", msg->line);
    } else {
        ESP_LOGI(TAG, "CH32 => %s", msg->line);
    }
}
/*
 * 把旧版文本协议行包装成统一消息结构后派发给上层。
 */
static void app_ch32_dispatch_legacy_line(const char *line)
{
    app_ch32_line_t msg = {0};
    msg.type = app_ch32_classify_legacy_line(line);
    msg.is_proto = false;
    strlcpy(msg.line, line, sizeof(msg.line));
    app_ch32_dispatch_msg(&msg);
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
     * 避免噪声或旧文本内容被误解析成协议帧。
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
     * CRC16 覆盖范围从 VER 开始，到 payload 结束。
     *
     * 帧头 SOF1/SOF2 不参与 CRC，这样接收端可以先用帧头做同步，
     * 再用版本、类型、命令、序号、长度和 payload 做完整性校验。
     */
    const uint16_t crc_expect = app_ch32_read_u16_le(&frame[7 + payload_len]);
    const uint16_t crc_actual = app_ch32_crc16_ibm(&frame[2], (size_t)(5U + payload_len));
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
    out->is_proto = true;          // 标记这条消息来自新版二进制协议，而不是旧文本行。
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
            snprintf(out->line, sizeof(out->line),
                     "PROTO NACK cmd=0x%02X seq=%u",
                     out->proto_cmd,
                     (unsigned)out->proto_seq);
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
         * 生成一行可读文本。
         *
         * 这样串口日志、UI 调试文本和旧版文本协议可以共用同一个 line 字段，
         * 上层调试时不用再手工解码二进制 payload。
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
 * UART 接收任务，持续读取 CH32 数据，同时识别二进制帧和旧版文本行。
 */
static void app_ch32_link_rx_task(void *arg)
{
    /*
     * ch 是每次从 UART 读取的单字节。
     * 本任务按字节解析，是因为需要同时兼容：
     * - 新版二进制帧：0x55 0xAA 开头，后面按长度收满；
     * - 旧版文本行：[ACK] / [STS] / [ERR] / [DBG]，以换行结束。
     */
    uint8_t ch = 0;

    /*
     * 旧文本协议行缓冲。
     *
     * CH32 旧固件会发送可读字符串，每收到 '\n' 就把一整行派发给上层。
     */
    char line_buf[APP_CH32_LINK_LINE_MAX] = {0};
    size_t line_len = 0;

    /*
     * 新版二进制协议缓冲。
     *
     * proto_len 表示已经收到多少字节；
     * proto_expect 表示根据 LEN 字段算出来的完整帧长度；
     * proto_active 表示当前正在收一帧二进制协议。
     */
    uint8_t proto_buf[APP_CH32_PROTO_MAX_FRAME] = {0};
    size_t proto_len = 0;
    size_t proto_expect = 0;
    bool proto_active = false;

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
         * 如果当前还没有进入二进制帧解析状态，
         * 遇到 SOF1 就先假设新版协议帧开始。
         *
         * 这里先不立刻认定成功，因为 0x55 也可能只是旧文本或噪声中的一个普通字节。
         */
        if (!proto_active) {
            if (ch == APP_CH32_PROTO_SOF1) {
                proto_active = true;
                proto_len = 1;
                proto_expect = 0;
                proto_buf[0] = ch;
                continue;
            }
        }

        /*
         * 二进制协议解析分支。
         *
         * 一旦 proto_active=true，后续字节优先按协议帧处理。
         * 如果发现第二个字节不是 SOF2，再回退到旧文本行解析。
         */
        if (proto_active) {
            if ((proto_len == 1U) && (ch != APP_CH32_PROTO_SOF2)) {
                /*
                 * 回退场景：
                 * 已经收到了 SOF1，但第二个字节不是 SOF2，
                 * 说明这不是新版协议帧。
                 *
                 * 为了不丢数据，先把 SOF1 尝试放回文本行缓冲，
                 * 再把当前 ch 按旧文本协议继续处理。
                 */
                proto_active = false;
                proto_len = 0;
                proto_expect = 0;
                if (line_len < sizeof(line_buf) - 1U && isprint((int)APP_CH32_PROTO_SOF1)) {
                    line_buf[line_len++] = (char)APP_CH32_PROTO_SOF1;
                }
                if (ch == '\n') {
                    if (line_len > 0U) {
                        line_buf[line_len] = '\0';
                        app_ch32_dispatch_legacy_line(line_buf);
                        line_len = 0;
                        line_buf[0] = '\0';
                    }
                } else if ((ch != '\r') && (isprint((int)ch) != 0)) {
                    if (line_len < sizeof(line_buf) - 1U) {
                        line_buf[line_len++] = (char)ch;
                    }
                }
                continue;
            }

            /*
             * 把当前字节追加到协议缓冲。
             *
             * 如果超过缓冲上限，说明这帧异常，直接丢弃并重新等待下一帧。
             */
            if (proto_len < sizeof(proto_buf)) {
                proto_buf[proto_len++] = ch;
            } else {
                proto_active = false;
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
                    proto_active = false;
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
             *
             * 先调用 app_ch32_parse_proto_frame() 做版本、长度、CRC 和字段解析；
             * 解析成功才派发给上层，否则静默丢弃坏帧。
             */
            if ((proto_expect >= APP_CH32_PROTO_MIN_FRAME) && (proto_len == proto_expect)) {
                app_ch32_line_t msg = {0};
                if (app_ch32_parse_proto_frame(proto_buf, proto_len, &msg)) {
                    app_ch32_dispatch_msg(&msg);
                }
                proto_active = false;
                proto_len = 0;
                proto_expect = 0;
            }
            continue;
        }

        /*
         * 旧文本协议解析分支。
         *
         * 旧固件使用 '\r' / '\n' 作为行结束，常见输出类似：
         * [ACK] A
         * [STS] CH32_READY WEIGHT=123
         * [ERR] LIMIT_TIMEOUT
         */
        if (ch == '\r') {
            continue;
        }

        /*
         * 换行表示一条旧文本消息结束。
         * line_len > 0 时才派发，避免空行干扰状态机。
         */
        if (ch == '\n') {
            if (line_len > 0U) {
                line_buf[line_len] = '\0';
                app_ch32_dispatch_legacy_line(line_buf);
                line_len = 0;
                line_buf[0] = '\0';
            }
            continue;
        }

        /*
         * 过滤不可打印字符。
         *
         * 旧文本协议只保留可读字符，避免二进制噪声进入 UI 或日志。
         */
        if (isprint((int)ch) == 0) {
            continue;
        }

        /*
         * 普通字符追加到旧文本行缓冲。
         * 如果行太长，就先把当前缓冲作为一行派发，避免数组越界。
         */
        if (line_len < sizeof(line_buf) - 1U) {
            line_buf[line_len++] = (char)ch;
        } else {
            line_buf[line_len] = '\0';
            app_ch32_dispatch_legacy_line(line_buf);
            line_len = 0;
            line_buf[0] = '\0';
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
 * 按旧版协议发送单字符命令，适配简单 CH32 固件。
 */
static esp_err_t app_ch32_link_send_legacy_cmd(char cmd)
{
    /*
     * 旧版文本命令格式固定为：
     * @ + 单字符命令 + '\n'
     *
     * 例如：
     * @P\n 表示探测 ready；
     * @A\n 表示开始接驳；
     * @O\n 表示开门。
     */
    char frame[4] = {'@', cmd, '\n', '\0'};

    // 通过 UART 向 CH32 发送旧文本命令帧，只发送前三个有效字符，不发送结尾 '\0'。
    int written = uart_write_bytes(s_ctx.uart_num, frame, 3);
    ESP_RETURN_ON_FALSE(written == 3, ESP_FAIL, TAG, "uart_write_bytes legacy failed");

    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "ESP32 => CH32 legacy : %s", frame);

    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 按新版二进制协议发送命令帧。
 *
 * 新协议比旧文本协议多了版本号、命令码、序号、payload 长度和 CRC16，
 * 更适合承载结构化状态和后续扩展字段。
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

    /*
     * frame[] 是即将写入 UART 的完整二进制帧。
     * idx 始终指向下一个待写入位置，用连续自增保证字段顺序和协议定义一致。
     */
    uint8_t frame[APP_CH32_PROTO_MAX_FRAME] = {0};

    /*
     * 每发送一帧命令，序号自增。
     * 当前实现主要用它做日志和后续扩展，ACK 匹配仍然以命令码为主。
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
     * CRC 使用小端写入，和解析端 app_ch32_read_u16_le() 对应。
     */
    const uint16_t crc = app_ch32_crc16_ibm(&frame[2], (size_t)(5U + payload_len));
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
 * 等待指定命令的 ACK，用事件组实现同步等待和超时保护。
 */
static esp_err_t app_ch32_link_wait_ack_for_cmd(char cmd, uint32_t timeout_ms)
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
     * - 收到目标命令对应的 ACK，返回 ESP_OK；
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
                                               CH32_EVT_ACK,
                                               pdTRUE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(this_wait));
        elapsed_ms += this_wait;

        /*
         * 这一片时间里没有 ACK，就继续等下一片。
         */
        if ((bits & CH32_EVT_ACK) == 0U) {
            continue;
        }

        /*
         * 收到了 ACK，还要确认 ACK 对应的是当前等待的命令。
         * 例如正在等 'A'，但串口里残留的是 'P' 的 ACK，就不能当成成功。
         */
        if (s_ctx.last_ack_cmd == cmd) {
            // 正常返回 ESP_OK，表示该步骤执行成功。
            return ESP_OK;
        }

        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "ack mismatch, expect=%c actual=%c", cmd, s_ctx.last_ack_cmd);
        s_ctx.last_ack_cmd = 0; // 清掉不匹配 ACK，继续等待目标命令的 ACK。
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
     * ready / proto_online / has_weight 都必须从 false 开始，
     * 避免复位或重新初始化时沿用上一轮通信留下的旧状态。
     */
    s_ctx.ready = false;       // 还没有收到 CH32_READY 或协议状态帧，所以暂时认为副控未就绪。
    s_ctx.proto_online = false; // 尚未收到新版二进制协议帧，发送时仍需要准备好回退旧文本协议。
    s_ctx.has_weight = false;  // 尚未收到称重数据，app_ch32_link_last_weight() 不应返回旧重量。
    s_ctx.last_ack_cmd = 0;    // 清空最近 ACK 对应的命令字符，避免旧 ACK 影响下一次等待。
    s_ctx.next_seq = 0;        // 新版协议帧序号从 0 开始，发送时先自增再写入帧头。

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
     * 同时兼容新版二进制协议帧和旧版文本行。
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

    // 清除事件组 bit，避免旧事件影响下一次等待。
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK);
    s_ctx.last_ack_cmd = 0; // 同步清掉最近 ACK 命令字符，确保这次等待只认新 ACK。

    /*
     * 仍然优先尝试新版二进制协议。
     * 如果 CH32 新固件在线，正常会很快返回协议 ACK。
     */
    const app_ch32_proto_cmd_t proto_cmd = app_ch32_legacy_cmd_to_proto(cmd);
    if (proto_cmd != APP_CH32_PROTO_CMD_NONE) {
        esp_err_t ret = app_ch32_link_send_proto(proto_cmd, NULL, 0, NULL);
        if (ret == ESP_OK) {
            /*
             * 新协议 ACK 先用一个较短等待窗口。
             *
             * 这样如果对端还是旧固件，不会把全部 timeout 都耗在新版协议上，
             * 可以尽快回退旧文本命令。
             */
            uint32_t first_wait = APP_CH32_LINK_PROTO_ACK_FIRST_WAIT_MS;
            if (first_wait > timeout_ms) {
                first_wait = timeout_ms;
            }

            ret = app_ch32_link_wait_ack_for_cmd(cmd, first_wait);
            if (ret == ESP_OK) {
                // 正常返回 ESP_OK，表示该步骤执行成功。
                return ESP_OK;
            }

            // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
            ESP_LOGW(TAG, "proto ack timeout for %c, fallback legacy", cmd);
        } else {
            // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
            ESP_LOGW(TAG, "proto send failed for %c: %s, fallback legacy", cmd, esp_err_to_name(ret));
        }

        // 清除事件组 bit，避免旧事件影响下一次等待。
        xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK);
        s_ctx.last_ack_cmd = 0; // 回退旧协议前再次清空 ACK 状态，避免新旧两次发送互相串扰。
    }

    /*
     * 旧文本协议发送。
     * 如果这里发送失败，说明 UART 写入本身有问题，直接返回错误。
     */
    ESP_RETURN_ON_ERROR(app_ch32_link_send_legacy_cmd(cmd), TAG, "legacy send failed");

    /*
     * 等待旧文本协议 ACK。
     * CH32 旧固件通常会回 [ACK] A / [ACK] P 这种行文本，RX 任务会解析后置位 CH32_EVT_ACK。
     */
    return app_ch32_link_wait_ack_for_cmd(cmd, timeout_ms);
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
    if (s_ctx.ready) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }

    // 清除事件组 bit，避免旧事件影响下一次等待。
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);

    /*
     * 新协议探测窗口。
     *
     * 这里不会直接使用完整 timeout_ms，而是先给新版协议一个较短机会；
     * 如果没有等到 ready，再回退旧版 @P 文本探测。
     */
    uint32_t proto_wait = APP_CH32_LINK_PROTO_PROBE_MS;
    if (proto_wait > timeout_ms) {
        proto_wait = timeout_ms;
    }

    esp_err_t ret = app_ch32_link_send_proto(APP_CH32_PROTO_CMD_PROBE_READY, NULL, 0, NULL);
    if (ret == ESP_OK) {
        /*
         * 等待 RX 任务解析到 ready 状态。
         * 新协议中 STATUS / HEARTBEAT / EVENT 只要带 READY flag 或 ready 阶段，
         * app_ch32_apply_common_side_effects() 就会置位 CH32_EVT_READY。
         */
        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                               CH32_EVT_READY,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(proto_wait));
        if ((bits & CH32_EVT_READY) != 0U) {
            // 正常返回 ESP_OK，表示该步骤执行成功。
            return ESP_OK;
        }
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "proto probe timeout, fallback legacy probe");
    }

    /*
     * 回退旧文本探测命令 @P。
     * 旧版 CH32 固件收到后应回 [STS] CH32_READY 或类似状态行。
     */
    ESP_RETURN_ON_ERROR(app_ch32_link_send_legacy_cmd('P'), TAG, "legacy probe send failed");

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
    return s_ctx.ready;
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
