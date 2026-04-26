#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
 * CH32 <-> ESP32-P4 UART Link
 *
 * 新协议帧：
 *   SOF1 SOF2 VER TYPE CMD SEQ LEN PAYLOAD... CRC16_LO CRC16_HI
 *   0x55 0xAA 0x01 ...
 *
 * 兼容旧文本协议：
 *   ESP32 -> CH32 : @P\n @A\n ...
 *   CH32  -> ESP32: [ACK] ... [STS] ... [ERR] ... [DBG] ...
 *
 * 当前实现策略：
 *   1) 优先发送新二进制协议；
 *   2) 若超时未收到 ACK，则自动回退旧文本命令；
 *   3) RX 同时支持新协议帧和旧文本行。
 * ========================= */

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

#ifndef APP_CH32_LINK_READY_RETRY_MS
#define APP_CH32_LINK_READY_RETRY_MS      (300U)
#endif

#ifndef APP_CH32_LINK_ACK_POLL_MS
#define APP_CH32_LINK_ACK_POLL_MS         (50U)
#endif

#ifndef APP_CH32_LINK_PROTO_PROBE_MS
#define APP_CH32_LINK_PROTO_PROBE_MS      (250U)
#endif

#ifndef APP_CH32_LINK_PROTO_ACK_FIRST_WAIT_MS
#define APP_CH32_LINK_PROTO_ACK_FIRST_WAIT_MS   (350U)
#endif

#define APP_CH32_PROTO_SOF1               (0x55U)
#define APP_CH32_PROTO_SOF2               (0xAAU)
#define APP_CH32_PROTO_VER                (0x01U)

typedef enum {
    APP_CH32_LINE_UNKNOWN = 0,
    APP_CH32_LINE_ACK,
    APP_CH32_LINE_STATUS,
    APP_CH32_LINE_ERROR,
    APP_CH32_LINE_DEBUG,
    APP_CH32_LINE_PROTO_ACK,
    APP_CH32_LINE_PROTO_NACK,
    APP_CH32_LINE_PROTO_STATUS,
    APP_CH32_LINE_PROTO_EVENT,
    APP_CH32_LINE_PROTO_ERROR,
    APP_CH32_LINE_PROTO_HEARTBEAT,
} app_ch32_line_type_t;

typedef enum {
    APP_CH32_PROTO_TYPE_CMD       = 0x10,
    APP_CH32_PROTO_TYPE_ACK       = 0x11,
    APP_CH32_PROTO_TYPE_NACK      = 0x12,
    APP_CH32_PROTO_TYPE_STATUS    = 0x20,
    APP_CH32_PROTO_TYPE_EVENT     = 0x21,
    APP_CH32_PROTO_TYPE_ERROR     = 0x22,
    APP_CH32_PROTO_TYPE_HEARTBEAT = 0x23,
} app_ch32_proto_type_t;

typedef enum {
    APP_CH32_PROTO_CMD_NONE          = 0x00,
    APP_CH32_PROTO_CMD_PROBE_READY   = 0x01,
    APP_CH32_PROTO_CMD_START_DOCK    = 0x02,
    APP_CH32_PROTO_CMD_OPEN_DOOR     = 0x03,
    APP_CH32_PROTO_CMD_CLOSE_DOOR    = 0x04,
    APP_CH32_PROTO_CMD_EXTEND_TRAY   = 0x05,
    APP_CH32_PROTO_CMD_RETRACT_TRAY  = 0x06,
    APP_CH32_PROTO_CMD_QUERY_STATUS  = 0x07,
    APP_CH32_PROTO_CMD_READ_WEIGHT   = 0x08,
    APP_CH32_PROTO_CMD_ABORT         = 0x09,
    APP_CH32_PROTO_CMD_RESET_FAULT   = 0x0A,
} app_ch32_proto_cmd_t;

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

#define APP_CH32_FLAG_READY                (1U << 0)
#define APP_CH32_FLAG_BUSY                 (1U << 1)
#define APP_CH32_FLAG_LIMIT_DOOR_OPEN      (1U << 2)
#define APP_CH32_FLAG_LIMIT_DOOR_CLOSED    (1U << 3)
#define APP_CH32_FLAG_LIMIT_TRAY_OUT       (1U << 4)
#define APP_CH32_FLAG_LIMIT_TRAY_IN        (1U << 5)
#define APP_CH32_FLAG_CARGO_PRESENT        (1U << 6)
#define APP_CH32_FLAG_LOCKED               (1U << 7)

typedef struct {
    app_ch32_line_type_t type;
    char line[APP_CH32_LINK_LINE_MAX];

    bool is_proto;
    uint8_t proto_type;
    uint8_t proto_cmd;
    uint8_t proto_seq;
    uint16_t proto_flags;
    app_ch32_proto_stage_t proto_stage;
    uint8_t proto_detail;
    int32_t proto_weight_g;
    uint8_t payload[APP_CH32_LINK_PROTO_MAX_PAYLOAD];
    uint16_t payload_len;
} app_ch32_line_t;

typedef void (*app_ch32_line_cb_t)(const app_ch32_line_t *msg, void *user_ctx);

esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx);

esp_err_t app_ch32_link_send_cmd_and_wait_ack(char cmd, uint32_t timeout_ms);
esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms);

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
