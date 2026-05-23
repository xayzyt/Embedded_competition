#pragma once

/*
 * ESP32-P4 与 CH32 之间的 UART 协议层 (精简版)。
 *
 * 帧格式:
 *   SOF1 SOF2 TYPE CMD SEQ LEN [PAYLOAD] CRC16
 * CRC16-IBM 覆盖 SOF1 到 PAYLOAD 末尾。
 *
 * 已砍掉: VER 版本号、老协议 ASCII、单步调试命令
 * TYPE 合并: STATUS 统一承载 状态/事件/错误/心跳
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- 硬件引脚 -------------------- */
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

/* -------------------- 协议常量 -------------------- */
#define APP_CH32_PROTO_SOF1               (0x55U)
#define APP_CH32_PROTO_SOF2               (0xAAU)
#define APP_CH32_PROTO_OVERHEAD           (8U)   /* SOF1+SOF2+TYPE+CMD+SEQ+LEN+CRC16 */
#define APP_CH32_PROTO_MIN_FRAME          (APP_CH32_PROTO_OVERHEAD)

/* 帧类型 */
typedef enum {
    APP_CH32_PROTO_TYPE_CMD       = 0x10,
    APP_CH32_PROTO_TYPE_ACK       = 0x11,
    APP_CH32_PROTO_TYPE_NACK      = 0x12,
    APP_CH32_PROTO_TYPE_STATUS    = 0x20,  /* 合并: 状态/事件/错误/心跳 */
} app_ch32_proto_type_t;

/* 命令码 (比赛只需这些) */
typedef enum {
    APP_CH32_PROTO_CMD_NONE          = 0x00,
    APP_CH32_PROTO_CMD_PROBE_READY   = 0x01,
    APP_CH32_PROTO_CMD_START_DOCK    = 0x02,
    APP_CH32_PROTO_CMD_QUERY_STATUS  = 0x07,
    APP_CH32_PROTO_CMD_READ_WEIGHT   = 0x08,
    APP_CH32_PROTO_CMD_ABORT         = 0x09,
    APP_CH32_PROTO_CMD_RESET_FAULT   = 0x0A,
    APP_CH32_PROTO_CMD_OPEN_INNER_DOOR = 0x0B,
    APP_CH32_PROTO_CMD_SAFE_CLOSE    = 0x0C,
} app_ch32_proto_cmd_t;

/* 阶段 */
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

/* 错误码 */
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

/* Flags 位图 */
#define APP_CH32_FLAG_READY                (1U << 0)
#define APP_CH32_FLAG_BUSY                 (1U << 1)
#define APP_CH32_FLAG_LIMIT_DOOR_OPEN      (1U << 2)
#define APP_CH32_FLAG_LIMIT_DOOR_CLOSED    (1U << 3)
#define APP_CH32_FLAG_LIMIT_TRAY_OUT       (1U << 4)
#define APP_CH32_FLAG_LIMIT_TRAY_IN        (1U << 5)
#define APP_CH32_FLAG_CARGO_PRESENT        (1U << 6)
#define APP_CH32_FLAG_LOCKED               (1U << 7)

/* 解析后的消息类型 (精简) */
typedef enum {
    APP_CH32_LINE_UNKNOWN = 0,
    APP_CH32_LINE_PROTO_ACK,
    APP_CH32_LINE_PROTO_NACK,
    APP_CH32_LINE_PROTO_STATUS,  /* 合并: status/event/error/heartbeat */
} app_ch32_line_type_t;

/* 统一消息结构 */
typedef struct {
    app_ch32_line_type_t type;                               /* 解析后的消息类型。 */
    char line[APP_CH32_LINK_LINE_MAX];                       /* 可读摘要文本，用于日志和调试。 */

    uint8_t proto_type;                                      /* 协议帧 TYPE 字段原始值。 */
    uint8_t proto_cmd;                                       /* 协议帧 CMD 字段原始值。 */
    uint8_t proto_seq;                                       /* 协议帧 SEQ 字段原始值。 */
    uint16_t proto_flags;                                    /* STATUS 帧 flags 位图，参见 APP_CH32_FLAG_*。 */
    app_ch32_proto_stage_t proto_stage;                      /* STATUS 帧阶段枚举值。 */
    uint8_t proto_detail;                                    /* STATUS 帧错误码 / NACK 附带错误码。 */
    int32_t proto_weight_g;                                  /* STATUS 帧携带的称重值（克）。 */
    uint8_t payload[APP_CH32_LINK_PROTO_MAX_PAYLOAD];        /* 原始 payload 数据。 */
    uint16_t payload_len;                                    /* payload 实际字节数。 */
} app_ch32_line_t;

typedef void (*app_ch32_line_cb_t)(const app_ch32_line_t *msg, void *user_ctx);

/* -------------------- 公开接口 -------------------- */

esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx);

/* 发送命令并等待 ACK */
esp_err_t app_ch32_link_send_cmd_and_wait_ack(char cmd, uint32_t timeout_ms);

/* 主动探测 ready */
esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms);

/* 发送二进制协议帧 */
esp_err_t app_ch32_link_send_proto(app_ch32_proto_cmd_t cmd,
                                   const void *payload,
                                   uint8_t payload_len,
                                   uint8_t *out_seq);

bool app_ch32_link_is_ready(void);
bool app_ch32_link_last_weight(int32_t *out_weight_g);

const char *app_ch32_link_proto_stage_name(app_ch32_proto_stage_t stage);
const char *app_ch32_link_proto_error_name(uint8_t err);

#ifdef __cplusplus
}
#endif
