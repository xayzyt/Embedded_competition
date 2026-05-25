#ifndef __ESP32_COMM_H
#define __ESP32_COMM_H

#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compact ESP32 <-> CH32 UART protocol.
 *
 * Frame:
 *   SOF1 SOF2 TYPE CMD SEQ LEN [PAYLOAD] CRC16
 *
 * CRC16-IBM covers SOF1 through the last payload byte.
 * STATUS carries stage, error/detail, flags, and weight.
 */

#define ESP32_COMM_RX_FIFO_SIZE             128U
#define ESP32_COMM_PROTO_MAX_PAYLOAD        48U
#define ESP32_COMM_PROTO_OVERHEAD           8U
#define ESP32_COMM_PROTO_MIN_FRAME          ESP32_COMM_PROTO_OVERHEAD

#define ESP32_COMM_PROTO_SOF1               0x55U
#define ESP32_COMM_PROTO_SOF2               0xAAU

/* Frame type. Keep values aligned with APP_CH32_PROTO_TYPE_* on ESP32. */
#define ESP32_COMM_PROTO_TYPE_CMD           0x10U
#define ESP32_COMM_PROTO_TYPE_ACK           0x11U
#define ESP32_COMM_PROTO_TYPE_NACK          0x12U
#define ESP32_COMM_PROTO_TYPE_STATUS        0x20U

/* Command code. Keep values aligned with APP_CH32_PROTO_CMD_* on ESP32. */
#define ESP32_COMM_PROTO_CMD_NONE           0x00U
#define ESP32_COMM_PROTO_CMD_PROBE_READY    0x01U
#define ESP32_COMM_PROTO_CMD_START_DOCK     0x02U
#define ESP32_COMM_PROTO_CMD_QUERY_STATUS   0x07U
#define ESP32_COMM_PROTO_CMD_READ_WEIGHT    0x08U
#define ESP32_COMM_PROTO_CMD_ABORT          0x09U
#define ESP32_COMM_PROTO_CMD_RESET_FAULT    0x0AU
#define ESP32_COMM_PROTO_CMD_OPEN_INNER_DOOR 0x0BU
#define ESP32_COMM_PROTO_CMD_SAFE_CLOSE     0x0CU

/* Stage code. Keep values aligned with APP_CH32_STAGE_* on ESP32. */
#define ESP32_COMM_STAGE_UNKNOWN            0U
#define ESP32_COMM_STAGE_IDLE               1U
#define ESP32_COMM_STAGE_READY              2U
#define ESP32_COMM_STAGE_DOOR_OPENING       3U
#define ESP32_COMM_STAGE_DOOR_OPENED        4U
#define ESP32_COMM_STAGE_TRAY_EXTENDING     5U
#define ESP32_COMM_STAGE_TRAY_EXTENDED      6U
#define ESP32_COMM_STAGE_WAITING_CARGO      7U
#define ESP32_COMM_STAGE_CARGO_DETECTED     8U
#define ESP32_COMM_STAGE_TRAY_RETRACTING    9U
#define ESP32_COMM_STAGE_TRAY_RETRACTED     10U
#define ESP32_COMM_STAGE_DOOR_CLOSING       11U
#define ESP32_COMM_STAGE_SAFE_LOCKED        12U
#define ESP32_COMM_STAGE_COMPLETE           13U
#define ESP32_COMM_STAGE_FAULT              14U

/* Error code. Keep values aligned with APP_CH32_ERR_* on ESP32. */
#define ESP32_COMM_ERR_NONE                 0U
#define ESP32_COMM_ERR_TIMEOUT              1U
#define ESP32_COMM_ERR_LIMIT                2U
#define ESP32_COMM_ERR_WEIGHT               3U
#define ESP32_COMM_ERR_JAM                  4U
#define ESP32_COMM_ERR_SENSOR               5U
#define ESP32_COMM_ERR_SAFETY               6U
#define ESP32_COMM_ERR_BUSY                 7U
#define ESP32_COMM_ERR_UNKNOWN_CMD          8U
#define ESP32_COMM_ERR_BAD_CRC              9U
#define ESP32_COMM_ERR_INTERNAL             10U

/* STATUS flags. Keep values aligned with APP_CH32_FLAG_* on ESP32. */
#define ESP32_COMM_FLAG_READY               (1U << 0)
#define ESP32_COMM_FLAG_BUSY                (1U << 1)
#define ESP32_COMM_FLAG_LIMIT_DOOR_OPEN     (1U << 2)
#define ESP32_COMM_FLAG_LIMIT_DOOR_CLOSED   (1U << 3)
#define ESP32_COMM_FLAG_LIMIT_TRAY_OUT      (1U << 4)
#define ESP32_COMM_FLAG_LIMIT_TRAY_IN       (1U << 5)
#define ESP32_COMM_FLAG_CARGO_PRESENT       (1U << 6)
#define ESP32_COMM_FLAG_LOCKED              (1U << 7)

typedef struct
{
    uint8_t proto_type;
    uint8_t proto_cmd;
    uint8_t proto_seq;
    uint8_t payload_len;
    uint8_t payload[ESP32_COMM_PROTO_MAX_PAYLOAD];
} ESP32_Comm_Packet_t;

void ESP32_Comm_Init(void);
void ESP32_Comm_FlushRx(void);
uint8_t ESP32_Comm_ReadPacket(ESP32_Comm_Packet_t *out_pkt);

void ESP32_Comm_SendProtoAck(uint8_t cmd, uint8_t seq);
void ESP32_Comm_SendProtoNack(uint8_t cmd, uint8_t seq, uint8_t err_code);
void ESP32_Comm_SendProtoState(uint8_t proto_type,
                               uint8_t cmd,
                               uint8_t seq,
                               uint8_t stage,
                               uint8_t detail,
                               uint16_t flags,
                               int32_t weight_g);

#ifdef __cplusplus
}
#endif

#endif
