#include "esp32_com.h"

#include <string.h>

/*
 * ============================================================================
 * CH32 <-> ESP32 串口通信协议层 (精简版)
 *
 * 职责:
 * 1. USART1 中断收字节 → FIFO
 * 2. 从 FIFO 解析二进制协议帧
 * 3. 发送 ACK / NACK / STATUS 帧
 *
 * 已砍掉:
 * - 老协议 ASCII (@X\n)
 * - VER 版本号字段
 * - EVENT / ERROR / HEARTBEAT 独立类型 → 合并为 STATUS
 *
 * 帧格式: SOF1 SOF2 TYPE CMD SEQ LEN [PAYLOAD] CRC16
 * CRC16-IBM 覆盖 SOF1 到 PAYLOAD 末尾
 * ============================================================================
 */

/* -------------------- 接收 FIFO -------------------- */
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static volatile uint8_t s_rx_fifo[ESP32_COMM_RX_FIFO_SIZE];

/* -------------------- 协议解析状态机 -------------------- */
static uint8_t s_proto_state = 0;
static uint8_t s_proto_buf[ESP32_COMM_PROTO_MAX_PAYLOAD + ESP32_COMM_PROTO_OVERHEAD];
static uint8_t s_proto_index = 0;
static uint8_t s_proto_len = 0;
static uint8_t s_proto_cmd = 0;
static uint8_t s_proto_seq = 0;

/* 阻塞发送一个字节 */
static void esp32_comm_send_byte(uint8_t byte)
{
    USART_SendData(USART1, byte);
    while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {}
}

/* 发送字节数组 */
static void esp32_comm_send_bytes(const uint8_t *data, uint16_t len)
{
    if(data == 0) return;
    for(uint16_t i = 0; i < len; i++)
        esp32_comm_send_byte(data[i]);
}

/* CRC16-IBM */
static uint16_t esp32_comm_crc16_ibm(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for(uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for(uint8_t bit = 0; bit < 8U; bit++)
        {
            if(crc & 0x0001U) crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            else              crc >>= 1;
        }
    }
    return crc;
}

/* FIFO 压入一字节，满时覆盖最旧数据 */
static void esp32_comm_fifo_push(uint8_t ch)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    if(next == s_rx_tail)
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    s_rx_fifo[s_rx_head] = ch;
    s_rx_head = next;
}

/*
 * 二进制协议字节流解析状态机 (8 个状态)
 *
 * 0: 等 SOF1  1: 等 SOF2  2: 收 TYPE  3: 收 CMD
 * 4: 收 SEQ   5: 收 LEN   6: 收 PAYLOAD
 * 7: 收 CRC_L  8: 收 CRC_H + 校验
 */
static uint8_t esp32_comm_try_parse_proto(uint8_t ch, ESP32_Comm_Packet_t *pkt)
{
    uint16_t calc_crc, recv_crc;
    uint8_t frame_len;

    switch(s_proto_state)
    {
        case 0:
            if(ch == ESP32_COMM_PROTO_SOF1)
            {
                s_proto_buf[0] = ch;
                s_proto_index = 1U;
                s_proto_len = 0U;
                s_proto_cmd = 0U;
                s_proto_seq = 0U;
                s_proto_state = 1;
            }
            break;

        case 1:
            if(ch == ESP32_COMM_PROTO_SOF2)
            {
                s_proto_buf[s_proto_index++] = ch;
                s_proto_state = 2;
            }
            else if(ch == ESP32_COMM_PROTO_SOF1)
            {
                /* 连续 SOF1，以当前字节为新帧起点 */
                s_proto_buf[0] = ch;
                s_proto_index = 1U;
            }
            else
            {
                s_proto_state = 0;
                s_proto_index = 0U;
            }
            break;

        case 2:
            /* TYPE */
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 3;
            break;

        case 3:
            /* CMD */
            s_proto_cmd = ch;
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 4;
            break;

        case 4:
            /* SEQ */
            s_proto_seq = ch;
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 5;
            break;

        case 5:
            /* LEN */
            if(ch > ESP32_COMM_PROTO_MAX_PAYLOAD)
            {
                s_proto_state = 0;
                s_proto_index = 0U;
                ESP32_Comm_SendProtoNack(s_proto_cmd, s_proto_seq, ESP32_COMM_ERR_INTERNAL);
                break;
            }
            s_proto_len = ch;
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = (s_proto_len == 0U) ? 7 : 6;  /* skip PAYLOAD if LEN=0 */
            break;

        case 6:
            /* PAYLOAD */
            s_proto_buf[s_proto_index++] = ch;
            if(s_proto_index >= (uint8_t)(ESP32_COMM_PROTO_OVERHEAD - 2U + s_proto_len))
                s_proto_state = 7;
            break;

        case 7:
            /* CRC_L */
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 8;
            break;

        case 8:
            /* CRC_H + 校验 */
            s_proto_buf[s_proto_index++] = ch;
            frame_len = (uint8_t)(ESP32_COMM_PROTO_OVERHEAD - 2U + s_proto_len + 2U);  /* =6+LEN+2 */
            calc_crc = esp32_comm_crc16_ibm(s_proto_buf, (uint16_t)(frame_len - 2U));   /* CRC覆盖 6+LEN 字节 */
            recv_crc = (uint16_t)((uint16_t)s_proto_buf[frame_len - 2U] |
                                  ((uint16_t)s_proto_buf[frame_len - 1U] << 8));

            if(calc_crc == recv_crc)
            {
                memset(pkt, 0, sizeof(*pkt));
                pkt->proto_type = s_proto_buf[2];
                pkt->proto_cmd  = s_proto_buf[3];
                pkt->proto_seq  = s_proto_buf[4];
                pkt->payload_len = s_proto_len;
                if(s_proto_len > 0U)
                    memcpy(pkt->payload, &s_proto_buf[6], s_proto_len);

                s_proto_state = 0;
                s_proto_index = 0U;
                s_proto_len = 0U;
                return 1U;
            }
            else
            {
                ESP32_Comm_SendProtoNack(s_proto_cmd, s_proto_seq, ESP32_COMM_ERR_BAD_CRC);
            }
            s_proto_state = 0;
            s_proto_index = 0U;
            s_proto_len = 0U;
            break;

        default:
            s_proto_state = 0;
            s_proto_index = 0U;
            s_proto_len = 0U;
            break;
    }
    return 0U;
}

/* -------------------- 公开接口 -------------------- */

void ESP32_Comm_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    s_rx_head = 0;
    s_rx_tail = 0;
    s_proto_state = 0U;
    s_proto_index = 0U;
    s_proto_len = 0U;
    s_proto_cmd = 0U;
    s_proto_seq = 0U;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ClearFlag(USART1, USART_FLAG_RXNE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

uint8_t ESP32_Comm_HasData(void)
{
    return (s_rx_head != s_rx_tail) ? 1U : 0U;
}

uint8_t ESP32_Comm_ReadByte(char *out_ch)
{
    if((out_ch == 0) || (s_rx_head == s_rx_tail))
        return 0U;
    *out_ch = (char)s_rx_fifo[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    return 1U;
}

void ESP32_Comm_FlushRx(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_proto_state = 0U;
    s_proto_index = 0U;
    s_proto_len = 0U;
    s_proto_cmd = 0U;
    s_proto_seq = 0U;
}

/* 从 FIFO 逐字节取，喂给协议状态机，拼出完整帧返回 */
uint8_t ESP32_Comm_ReadPacket(ESP32_Comm_Packet_t *out_pkt)
{
    char ch = 0;
    if(out_pkt == 0) return 0U;
    while(ESP32_Comm_ReadByte(&ch))
    {
        if(esp32_comm_try_parse_proto((uint8_t)ch, out_pkt))
            return 1U;
    }
    return 0U;
}

/* 发送 ACK 帧 (LEN=0, 总长 8 字节) */
void ESP32_Comm_SendProtoAck(uint8_t cmd, uint8_t seq)
{
    uint8_t frame[8];
    frame[0] = ESP32_COMM_PROTO_SOF1;
    frame[1] = ESP32_COMM_PROTO_SOF2;
    frame[2] = ESP32_COMM_PROTO_TYPE_ACK;
    frame[3] = cmd;
    frame[4] = seq;
    frame[5] = 0U;  /* LEN=0 */
    uint16_t crc = esp32_comm_crc16_ibm(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)((crc >> 8) & 0xFFU);
    esp32_comm_send_bytes(frame, 8U);
}

/* 发送 NACK 帧 (LEN=1, 总长 9 字节) */
void ESP32_Comm_SendProtoNack(uint8_t cmd, uint8_t seq, uint8_t err_code)
{
    uint8_t frame[9];
    frame[0] = ESP32_COMM_PROTO_SOF1;
    frame[1] = ESP32_COMM_PROTO_SOF2;
    frame[2] = ESP32_COMM_PROTO_TYPE_NACK;
    frame[3] = cmd;
    frame[4] = seq;
    frame[5] = 1U;  /* LEN=1 */
    frame[6] = err_code;
    uint16_t crc = esp32_comm_crc16_ibm(frame, 7U);
    frame[7] = (uint8_t)(crc & 0xFFU);
    frame[8] = (uint8_t)((crc >> 8) & 0xFFU);
    esp32_comm_send_bytes(frame, 9U);
}

/* 发送 STATUS 帧 (LEN=8, 总长 16 字节)
 * payload: [stage][detail][flags_L][flags_H][weight_g 4B LE] */
void ESP32_Comm_SendProtoState(uint8_t proto_type,
                               uint8_t cmd,
                               uint8_t seq,
                               uint8_t stage,
                               uint8_t detail,
                               uint16_t flags,
                               int32_t weight_g)
{
    uint8_t frame[16];
    frame[0] = ESP32_COMM_PROTO_SOF1;
    frame[1] = ESP32_COMM_PROTO_SOF2;
    frame[2] = proto_type;
    frame[3] = cmd;
    frame[4] = seq;
    frame[5] = 8U;   /* LEN=8 */
    frame[6] = stage;
    frame[7] = detail;
    frame[8] = (uint8_t)(flags & 0xFFU);
    frame[9] = (uint8_t)((flags >> 8) & 0xFFU);
    frame[10] = (uint8_t)((uint32_t)weight_g & 0xFFU);
    frame[11] = (uint8_t)(((uint32_t)weight_g >> 8) & 0xFFU);
    frame[12] = (uint8_t)(((uint32_t)weight_g >> 16) & 0xFFU);
    frame[13] = (uint8_t)(((uint32_t)weight_g >> 24) & 0xFFU);
    uint16_t crc = esp32_comm_crc16_ibm(frame, 14U);
    frame[14] = (uint8_t)(crc & 0xFFU);
    frame[15] = (uint8_t)((crc >> 8) & 0xFFU);
    esp32_comm_send_bytes(frame, 16U);
}

/* -------------------- USART1 中断 -------------------- */

void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void USART1_IRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART1);
        esp32_comm_fifo_push(ch);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
