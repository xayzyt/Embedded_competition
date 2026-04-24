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
    if (frame_len < APP_CH32_PROTO_MIN_FRAME) {
        return false;
    }
    if ((frame[0] != APP_CH32_PROTO_SOF1) || (frame[1] != APP_CH32_PROTO_SOF2)) {
        return false;
    }
    if (frame[2] != APP_CH32_PROTO_VER) {
        return false;
    }
    const uint8_t payload_len = frame[6];
    if ((size_t)(APP_CH32_PROTO_OVERHEAD + payload_len) != frame_len) {
        return false;
    }
    const uint16_t crc_expect = app_ch32_read_u16_le(&frame[7 + payload_len]);
    const uint16_t crc_actual = app_ch32_crc16_ibm(&frame[2], (size_t)(5U + payload_len));
    if (crc_expect != crc_actual) {
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "proto crc mismatch, expect=0x%04x actual=0x%04x", crc_expect, crc_actual);
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->is_proto = true;
    out->proto_type = frame[3];
    out->proto_cmd = frame[4];
    out->proto_seq = frame[5];
    out->payload_len = payload_len;
    if (payload_len > 0U) {
        memcpy(out->payload, &frame[7], payload_len);
    }
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
    if ((out->type == APP_CH32_LINE_PROTO_STATUS) ||
        (out->type == APP_CH32_LINE_PROTO_EVENT) ||
        (out->type == APP_CH32_LINE_PROTO_ERROR) ||
        (out->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        if (payload_len >= 8U) {
            out->proto_stage = (app_ch32_proto_stage_t)out->payload[0];
            out->proto_detail = out->payload[1];
            out->proto_flags = app_ch32_read_u16_le(&out->payload[2]);
            out->proto_weight_g = app_ch32_read_i32_le(&out->payload[4]);
        }
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
    uint8_t ch = 0;
    char line_buf[APP_CH32_LINK_LINE_MAX] = {0};
    size_t line_len = 0;
    uint8_t proto_buf[APP_CH32_PROTO_MAX_FRAME] = {0};
    size_t proto_len = 0;
    size_t proto_expect = 0;
    bool proto_active = false;
    (void)arg;
    while (1) {
        // 从 UART 接收缓冲中读取 CH32 发来的数据。
        int len = uart_read_bytes(s_ctx.uart_num, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }
        if (!proto_active) {
            if (ch == APP_CH32_PROTO_SOF1) {
                proto_active = true;
                proto_len = 1;
                proto_expect = 0;
                proto_buf[0] = ch;
                continue;
            }
        }
        if (proto_active) {
            if ((proto_len == 1U) && (ch != APP_CH32_PROTO_SOF2)) {
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
            if (proto_len < sizeof(proto_buf)) {
                proto_buf[proto_len++] = ch;
            } else {
                proto_active = false;
                proto_len = 0;
                proto_expect = 0;
                continue;
            }
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
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (line_len > 0U) {
                line_buf[line_len] = '\0';
                app_ch32_dispatch_legacy_line(line_buf);
                line_len = 0;
                line_buf[0] = '\0';
            }
            continue;
        }
        if (isprint((int)ch) == 0) {
            continue;
        }
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
    char frame[4] = {'@', cmd, '\n', '\0'};
    // 通过 UART 向 CH32 发送命令或协议帧。
    int written = uart_write_bytes(s_ctx.uart_num, frame, 3);
    // 通过 UART 向 CH32 发送命令或协议帧。
    ESP_RETURN_ON_FALSE(written == 3, ESP_FAIL, TAG, "uart_write_bytes legacy failed");
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "ESP32 => CH32 legacy : %s", frame);
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
esp_err_t app_ch32_link_send_proto(app_ch32_proto_cmd_t cmd,
                                   const void *payload,
                                   uint8_t payload_len,
                                   uint8_t *out_seq)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(payload_len <= APP_CH32_LINK_PROTO_MAX_PAYLOAD,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "payload too large");
    uint8_t frame[APP_CH32_PROTO_MAX_FRAME] = {0};
    const uint8_t seq = ++s_ctx.next_seq;
    size_t idx = 0;
    frame[idx++] = APP_CH32_PROTO_SOF1;
    frame[idx++] = APP_CH32_PROTO_SOF2;
    frame[idx++] = APP_CH32_PROTO_VER;
    frame[idx++] = (uint8_t)APP_CH32_PROTO_TYPE_CMD;
    frame[idx++] = (uint8_t)cmd;
    frame[idx++] = seq;
    frame[idx++] = payload_len;
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if ((payload_len > 0U) && (payload != NULL)) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }
    const uint16_t crc = app_ch32_crc16_ibm(&frame[2], (size_t)(5U + payload_len));
    frame[idx++] = (uint8_t)(crc & 0xFFU);
    frame[idx++] = (uint8_t)((crc >> 8) & 0xFFU);
    // 通过 UART 向 CH32 发送命令或协议帧。
    int written = uart_write_bytes(s_ctx.uart_num, (const char *)frame, idx);
    // 通过 UART 向 CH32 发送命令或协议帧。
    ESP_RETURN_ON_FALSE(written == (int)idx, ESP_FAIL, TAG, "uart_write_bytes proto failed");
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
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
    uint32_t elapsed_ms = 0;
    uint32_t slice_ms = APP_CH32_LINK_ACK_POLL_MS;
    if (slice_ms == 0U) {
        slice_ms = 50U;
    }
    while (elapsed_ms < timeout_ms) {
        uint32_t this_wait = slice_ms;
        if ((elapsed_ms + this_wait) > timeout_ms) {
            this_wait = timeout_ms - elapsed_ms;
        }
        // 等待事件组 bit，适合 ACK/READY/MQTT 连接这类同步点。
        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                               CH32_EVT_ACK,
                                               pdTRUE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(this_wait));
        elapsed_ms += this_wait;
        if ((bits & CH32_EVT_ACK) == 0U) {
            continue;
        }
        if (s_ctx.last_ack_cmd == cmd) {
            // 正常返回 ESP_OK，表示该步骤执行成功。
            return ESP_OK;
        }
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "ack mismatch, expect=%c actual=%c", cmd, s_ctx.last_ack_cmd);
        s_ctx.last_ack_cmd = 0;
    }
    return ESP_ERR_TIMEOUT;
}
/*
 * 初始化 UART、事件组和接收任务，建立 ESP32-P4 与 CH32 的通信通道。
 */
esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(!s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    s_ctx.uart_num = APP_CH32_LINK_UART_PORT;
    s_ctx.cb = cb;
    s_ctx.user_ctx = user_ctx;
    s_ctx.ready = false;
    s_ctx.proto_online = false;
    s_ctx.has_weight = false;
    s_ctx.last_ack_cmd = 0;
    s_ctx.next_seq = 0;
    uart_config_t uart_cfg = {
        .baud_rate = APP_CH32_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(app_ch32_link_prepare_tx_idle(), TAG, "prepare tx idle failed");
    // 安装 UART 驱动并创建底层接收缓冲。
    ESP_RETURN_ON_ERROR(uart_driver_install(s_ctx.uart_num, APP_CH32_LINK_RX_BUF_SIZE, 0, 0, NULL, 0),
                        TAG,
                        // 安装 UART 驱动并创建底层接收缓冲。
                        "uart_driver_install failed");
    // 配置 UART 波特率、数据位、停止位和校验位。
    ESP_RETURN_ON_ERROR(uart_param_config(s_ctx.uart_num, &uart_cfg), TAG, "uart_param_config failed");
    // 设置 UART TX/RX 引脚，把串口外设映射到实际 GPIO。
    ESP_RETURN_ON_ERROR(uart_set_pin(s_ctx.uart_num,
                                     APP_CH32_LINK_TX_GPIO,
                                     APP_CH32_LINK_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        // 设置 UART TX/RX 引脚，把串口外设映射到实际 GPIO。
                        "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(APP_CH32_LINK_RX_GPIO, GPIO_PULLUP_ONLY),
                        TAG,
                        "set rx pull-up failed");
    // 创建事件组，用多个 bit 表示异步状态。
    s_ctx.event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_ctx.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group create failed");
    // 创建 FreeRTOS 后台任务，把耗时逻辑从主流程中拆出去。
    BaseType_t ok = xTaskCreate(app_ch32_link_rx_task, "ch32_rx", 4096, NULL, 8, &s_ctx.rx_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task create failed");
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
 * 释放 UART、事件组和任务资源，用于停止或重启通信模块。
 */
esp_err_t app_ch32_link_deinit(void)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_ctx.rx_task != NULL) {
        // 删除当前或指定任务，通常用于停止后台循环。
        vTaskDelete(s_ctx.rx_task);
        s_ctx.rx_task = NULL;
    }
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_ctx.event_group != NULL) {
        vEventGroupDelete(s_ctx.event_group);
        s_ctx.event_group = NULL;
    }
    ESP_RETURN_ON_ERROR(uart_driver_delete(s_ctx.uart_num), TAG, "uart_driver_delete failed");
    memset(&s_ctx, 0, sizeof(s_ctx));
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 向 CH32 发送控制命令，内部会根据协议状态选择合适发送方式。
 */
esp_err_t app_ch32_link_send_cmd(char cmd)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    app_ch32_proto_cmd_t proto_cmd = app_ch32_legacy_cmd_to_proto(cmd);
    if (proto_cmd != APP_CH32_PROTO_CMD_NONE) {
        esp_err_t ret = app_ch32_link_send_proto(proto_cmd, NULL, 0, NULL);
        if (ret == ESP_OK) {
            // 正常返回 ESP_OK，表示该步骤执行成功。
            return ESP_OK;
        }
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "proto send failed for %c, fallback legacy", cmd);
    }
    return app_ch32_link_send_legacy_cmd(cmd);
}
/*
 * 发送命令并等待 ACK，适合需要确认副控已收到命令的关键动作。
 */
esp_err_t app_ch32_link_send_cmd_and_wait_ack(char cmd, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    // 清除事件组 bit，避免旧事件影响下一次等待。
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK);
    s_ctx.last_ack_cmd = 0;
    const app_ch32_proto_cmd_t proto_cmd = app_ch32_legacy_cmd_to_proto(cmd);
    if (proto_cmd != APP_CH32_PROTO_CMD_NONE) {
        esp_err_t ret = app_ch32_link_send_proto(proto_cmd, NULL, 0, NULL);
        if (ret == ESP_OK) {
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
        s_ctx.last_ack_cmd = 0;
    }
    ESP_RETURN_ON_ERROR(app_ch32_link_send_legacy_cmd(cmd), TAG, "legacy send failed");
    return app_ch32_link_wait_ack_for_cmd(cmd, timeout_ms);
}
/*
 * 主动探测 CH32 是否在线且 ready，常用于系统启动阶段。
 */
esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_ctx.ready) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    // 清除事件组 bit，避免旧事件影响下一次等待。
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);
    esp_err_t ret = ESP_FAIL;
    uint32_t proto_wait = APP_CH32_LINK_PROTO_PROBE_MS;
    if (proto_wait > timeout_ms) {
        proto_wait = timeout_ms;
    }
    ret = app_ch32_link_send_proto(APP_CH32_PROTO_CMD_PROBE_READY, NULL, 0, NULL);
    if (ret == ESP_OK) {
        // 等待事件组 bit，适合 ACK/READY/MQTT 连接这类同步点。
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
    ESP_RETURN_ON_ERROR(app_ch32_link_send_legacy_cmd('P'), TAG, "legacy probe send failed");
    // 等待事件组 bit，适合 ACK/READY/MQTT 连接这类同步点。
    EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                           CH32_EVT_READY,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return ((bits & CH32_EVT_READY) != 0U) ? ESP_OK : ESP_ERR_TIMEOUT;
}
/*
 * 等待 CH32 上报 ready，用于主控启动后确认副控可用。
 */
esp_err_t app_ch32_link_wait_ready(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_ctx.ready) {
        // 正常返回 ESP_OK，表示该步骤执行成功。
        return ESP_OK;
    }
    uint32_t elapsed_ms = 0;
    uint32_t slice_ms = APP_CH32_LINK_READY_RETRY_MS;
    if (slice_ms == 0U) {
        slice_ms = 300U;
    }
    while (elapsed_ms < timeout_ms) {
        uint32_t this_wait = slice_ms;
        if ((elapsed_ms + this_wait) > timeout_ms) {
            this_wait = timeout_ms - elapsed_ms;
        }
        if (app_ch32_link_probe_ready(this_wait) == ESP_OK) {
            // 正常返回 ESP_OK，表示该步骤执行成功。
            return ESP_OK;
        }
        elapsed_ms += this_wait;
    }
    return ESP_ERR_TIMEOUT;
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
