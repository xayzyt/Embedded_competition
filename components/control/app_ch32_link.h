#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
// CH32 UART 协议接口：控制器通过它发送动作命令并接收机械状态。

#ifdef __cplusplus
extern "C" {
#endif
// UART 与 CH32 从控通信的默认引脚、波特率和协议缓冲区参数。
#ifndef APP_CH32_LINK_UART_PORT
#define APP_CH32_LINK_UART_PORT           (1)
#endif
#ifndef APP_CH32_LINK_TX_GPIO
#define APP_CH32_LINK_TX_GPIO             (21)
#endif
#ifndef APP_CH32_LINK_RX_GPIO
#define APP_CH32_LINK_RX_GPIO             (22)
#endif
#ifndef APP_CH32_LINK_BAUD_RATE
#define APP_CH32_LINK_BAUD_RATE           (115200)
#endif
#ifndef APP_CH32_LINK_RX_BUF_SIZE
#define APP_CH32_LINK_RX_BUF_SIZE         (1024)
#endif
#ifndef APP_CH32_LINK_LINE_MAX
#define APP_CH32_LINK_LINE_MAX            (128)
#endif
#ifndef APP_CH32_LINK_PROTO_MAX_PAYLOAD
#define APP_CH32_LINK_PROTO_MAX_PAYLOAD   (48)
#endif
#ifndef APP_CH32_LINK_ACK_POLL_MS
#define APP_CH32_LINK_ACK_POLL_MS         (50U)
#endif
#define APP_CH32_PROTO_SOF1               (0x55U)
#define APP_CH32_PROTO_SOF2               (0xAAU)
#define APP_CH32_PROTO_OVERHEAD           (8U)
#define APP_CH32_PROTO_MIN_FRAME          (APP_CH32_PROTO_OVERHEAD)
// CH32 二进制协议帧类型。
typedef enum {
    APP_CH32_PROTO_TYPE_CMD       = 0x10,
    APP_CH32_PROTO_TYPE_ACK       = 0x11,
    APP_CH32_PROTO_TYPE_NACK      = 0x12,
    APP_CH32_PROTO_TYPE_STATUS    = 0x20,
} app_ch32_proto_type_t;
// 发送给 CH32 的控制命令。
typedef enum {
    APP_CH32_PROTO_CMD_NONE          = 0x00,
    APP_CH32_PROTO_CMD_PROBE_READY   = 0x01,
    APP_CH32_PROTO_CMD_START_DOCK    = 0x02,
    APP_CH32_PROTO_CMD_ABORT         = 0x09,
    APP_CH32_PROTO_CMD_RESET_FAULT   = 0x0A,
    APP_CH32_PROTO_CMD_OPEN_INNER_DOOR = 0x0B,
    APP_CH32_PROTO_CMD_SAFE_CLOSE    = 0x0C,
} app_ch32_proto_cmd_t;
// CH32 上报的机械执行阶段。
typedef enum {
    APP_CH32_STAGE_UNKNOWN         = 0,
    APP_CH32_STAGE_IDLE            = 1,
    APP_CH32_STAGE_READY           = 2,
    APP_CH32_STAGE_DOOR_OPENING    = 3,
    APP_CH32_STAGE_DOOR_OPENED     = 4,
    APP_CH32_STAGE_TRAY_EXTENDING  = 5,
    APP_CH32_STAGE_TRAY_EXTENDED   = 6,
    APP_CH32_STAGE_WAITING_CARGO   = 7,
    APP_CH32_STAGE_CARGO_DETECTED  = 8,
    APP_CH32_STAGE_TRAY_RETRACTING = 9,
    APP_CH32_STAGE_TRAY_RETRACTED  = 10,
    APP_CH32_STAGE_DOOR_CLOSING    = 11,
    APP_CH32_STAGE_SAFE_LOCKED     = 12,
    APP_CH32_STAGE_COMPLETE        = 13,
    APP_CH32_STAGE_FAULT           = 14,
} app_ch32_proto_stage_t;
// CH32 协议错误码。
typedef enum {
    APP_CH32_ERR_NONE         = 0,
    APP_CH32_ERR_TIMEOUT      = 1,
    APP_CH32_ERR_LIMIT        = 2,
    APP_CH32_ERR_WEIGHT       = 3,
    APP_CH32_ERR_JAM          = 4,
    APP_CH32_ERR_SENSOR       = 5,
    APP_CH32_ERR_SAFETY       = 6,
    APP_CH32_ERR_BUSY         = 7,
    APP_CH32_ERR_UNKNOWN_CMD  = 8,
    APP_CH32_ERR_BAD_CRC      = 9,
    APP_CH32_ERR_INTERNAL     = 10,
} app_ch32_proto_error_t;
// CH32 状态 flags，每一位对应一个限位/传感器/锁止状态。
#define APP_CH32_FLAG_READY                (1U << 0)
#define APP_CH32_FLAG_BUSY                 (1U << 1)
#define APP_CH32_FLAG_LIMIT_DOOR_OPEN      (1U << 2)
#define APP_CH32_FLAG_LIMIT_DOOR_CLOSED    (1U << 3)
#define APP_CH32_FLAG_LIMIT_TRAY_OUT       (1U << 4)
#define APP_CH32_FLAG_LIMIT_TRAY_IN        (1U << 5)
#define APP_CH32_FLAG_CARGO_PRESENT        (1U << 6)
#define APP_CH32_FLAG_LOCKED               (1U << 7)
// 接收到的一行/一帧数据类型。
typedef enum {
    APP_CH32_LINE_UNKNOWN = 0,
    APP_CH32_LINE_PROTO_ACK,
    APP_CH32_LINE_PROTO_NACK,
    APP_CH32_LINE_PROTO_STATUS,
} app_ch32_line_type_t;
// 解析后的 CH32 消息，兼容文本行和二进制协议帧。
typedef struct {
    app_ch32_line_type_t type;                  // 消息类型。
    char line[APP_CH32_LINK_LINE_MAX];          // 原始文本行，调试或兼容旧协议使用。
    uint8_t proto_type;                         // 二进制协议类型。
    uint8_t proto_cmd;                          // 对应命令。
    uint8_t proto_seq;                          // 序号，用于 ACK 匹配。
    uint16_t proto_flags;                       // 状态 flags。
    app_ch32_proto_stage_t proto_stage;         // 机械阶段。
    uint8_t proto_detail;                       // NACK/状态细节码。
    int32_t proto_weight_g;                     // 货物重量，单位 g。
    uint8_t payload[APP_CH32_LINK_PROTO_MAX_PAYLOAD]; // 原始 payload。
    uint16_t payload_len;                       // payload 长度。
} app_ch32_line_t;
// CH32 消息回调，由控制器注册。
typedef void (*app_ch32_line_cb_t)(const app_ch32_line_t *msg, void *user_ctx);
// 初始化 UART、接收任务和回调。
esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx);
// 发送二进制协议命令并等待匹配 ACK。
esp_err_t app_ch32_link_send_proto_cmd_and_wait_ack(app_ch32_proto_cmd_t cmd, uint32_t timeout_ms);
// 探测 CH32 是否 ready。
esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms);
// 返回最近一次 ready 状态是否仍然新鲜。
bool app_ch32_link_is_ready(void);
// 协议枚举转日志/UI 文案。
const char *app_ch32_link_proto_stage_name(app_ch32_proto_stage_t stage);
const char *app_ch32_link_proto_error_name(uint8_t err);
#ifdef __cplusplus
}
#endif
