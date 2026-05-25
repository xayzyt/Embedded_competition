#include "esp32_com.h"

#include <string.h>

/*
 * UART protocol layer for ESP32 <-> CH32.
 *
 * Responsibilities:
 * 1. Push USART1 RX bytes into a small FIFO from the ISR.
 * 2. Parse binary protocol frames from the FIFO.
 * 3. Send ACK, NACK, and STATUS frames back to ESP32.
 */

typedef enum
{
    ESP32_COMM_PARSE_WAIT_SOF1 = 0,
    ESP32_COMM_PARSE_WAIT_SOF2,
    ESP32_COMM_PARSE_TYPE,
    ESP32_COMM_PARSE_CMD,
    ESP32_COMM_PARSE_SEQ,
    ESP32_COMM_PARSE_LEN,
    ESP32_COMM_PARSE_PAYLOAD,
    ESP32_COMM_PARSE_CRC_LO,
    ESP32_COMM_PARSE_CRC_HI,
} esp32_comm_parser_state_t;

static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static volatile uint8_t s_rx_fifo[ESP32_COMM_RX_FIFO_SIZE];

static esp32_comm_parser_state_t s_proto_state = ESP32_COMM_PARSE_WAIT_SOF1;
static uint8_t s_proto_frame[ESP32_COMM_PROTO_OVERHEAD + ESP32_COMM_PROTO_MAX_PAYLOAD];
static uint8_t s_proto_index = 0;
static uint8_t s_proto_payload_len = 0;
static uint8_t s_proto_cmd = 0;
static uint8_t s_proto_seq = 0;

static void esp32_comm_reset_parser(void)
{
    s_proto_state = ESP32_COMM_PARSE_WAIT_SOF1;
    s_proto_index = 0U;
    s_proto_payload_len = 0U;
    s_proto_cmd = 0U;
    s_proto_seq = 0U;
}

static void esp32_comm_send_byte(uint8_t byte)
{
    USART_SendData(USART1, byte);
    while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {}
}

static void esp32_comm_send_bytes(const uint8_t *data, uint16_t len)
{
    if(data == 0) return;
    for(uint16_t i = 0; i < len; i++)
    {
        esp32_comm_send_byte(data[i]);
    }
}

static uint16_t esp32_comm_crc16_ibm(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for(uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for(uint8_t bit = 0; bit < 8U; bit++)
        {
            if((crc & 0x0001U) != 0U)
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            else
                crc >>= 1;
        }
    }
    return crc;
}

static void esp32_comm_fifo_push(uint8_t ch)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    if(next == s_rx_tail)
    {
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    }

    s_rx_fifo[s_rx_head] = ch;
    s_rx_head = next;
}

static uint8_t esp32_comm_has_data(void)
{
    return (s_rx_head != s_rx_tail) ? 1U : 0U;
}

static uint8_t esp32_comm_read_byte(char *out_ch)
{
    if((out_ch == 0) || (s_rx_head == s_rx_tail))
        return 0U;

    *out_ch = (char)s_rx_fifo[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    return 1U;
}

static void esp32_comm_send_frame(uint8_t proto_type,
                                  uint8_t cmd,
                                  uint8_t seq,
                                  const uint8_t *payload,
                                  uint8_t payload_len)
{
    uint8_t frame[ESP32_COMM_PROTO_OVERHEAD + ESP32_COMM_PROTO_MAX_PAYLOAD];
    uint8_t idx = 0U;

    if(payload_len > ESP32_COMM_PROTO_MAX_PAYLOAD)
        return;
    if((payload_len > 0U) && (payload == 0))
        return;

    frame[idx++] = ESP32_COMM_PROTO_SOF1;
    frame[idx++] = ESP32_COMM_PROTO_SOF2;
    frame[idx++] = proto_type;
    frame[idx++] = cmd;
    frame[idx++] = seq;
    frame[idx++] = payload_len;

    if(payload_len > 0U)
    {
        memcpy(&frame[idx], payload, payload_len);
        idx = (uint8_t)(idx + payload_len);
    }

    {
        uint16_t crc = esp32_comm_crc16_ibm(frame, idx);
        frame[idx++] = (uint8_t)(crc & 0xFFU);
        frame[idx++] = (uint8_t)((crc >> 8) & 0xFFU);
    }

    esp32_comm_send_bytes(frame, idx);
}

static uint8_t esp32_comm_parse_byte(uint8_t ch, ESP32_Comm_Packet_t *pkt)
{
    switch(s_proto_state)
    {
        case ESP32_COMM_PARSE_WAIT_SOF1:
            if(ch == ESP32_COMM_PROTO_SOF1)
            {
                esp32_comm_reset_parser();
                s_proto_frame[0] = ch;
                s_proto_index = 1U;
                s_proto_state = ESP32_COMM_PARSE_WAIT_SOF2;
            }
            break;

        case ESP32_COMM_PARSE_WAIT_SOF2:
            if(ch == ESP32_COMM_PROTO_SOF2)
            {
                s_proto_frame[s_proto_index++] = ch;
                s_proto_state = ESP32_COMM_PARSE_TYPE;
            }
            else if(ch == ESP32_COMM_PROTO_SOF1)
            {
                s_proto_frame[0] = ch;
                s_proto_index = 1U;
            }
            else
            {
                esp32_comm_reset_parser();
            }
            break;

        case ESP32_COMM_PARSE_TYPE:
            s_proto_frame[s_proto_index++] = ch;
            s_proto_state = ESP32_COMM_PARSE_CMD;
            break;

        case ESP32_COMM_PARSE_CMD:
            s_proto_cmd = ch;
            s_proto_frame[s_proto_index++] = ch;
            s_proto_state = ESP32_COMM_PARSE_SEQ;
            break;

        case ESP32_COMM_PARSE_SEQ:
            s_proto_seq = ch;
            s_proto_frame[s_proto_index++] = ch;
            s_proto_state = ESP32_COMM_PARSE_LEN;
            break;

        case ESP32_COMM_PARSE_LEN:
            if(ch > ESP32_COMM_PROTO_MAX_PAYLOAD)
            {
                ESP32_Comm_SendProtoNack(s_proto_cmd, s_proto_seq, ESP32_COMM_ERR_INTERNAL);
                esp32_comm_reset_parser();
                break;
            }

            s_proto_payload_len = ch;
            s_proto_frame[s_proto_index++] = ch;
            s_proto_state = (s_proto_payload_len == 0U) ?
                ESP32_COMM_PARSE_CRC_LO : ESP32_COMM_PARSE_PAYLOAD;
            break;

        case ESP32_COMM_PARSE_PAYLOAD:
            s_proto_frame[s_proto_index++] = ch;
            if(s_proto_index >= (uint8_t)(6U + s_proto_payload_len))
            {
                s_proto_state = ESP32_COMM_PARSE_CRC_LO;
            }
            break;

        case ESP32_COMM_PARSE_CRC_LO:
            s_proto_frame[s_proto_index++] = ch;
            s_proto_state = ESP32_COMM_PARSE_CRC_HI;
            break;

        case ESP32_COMM_PARSE_CRC_HI:
        {
            uint8_t frame_len;
            uint16_t calc_crc;
            uint16_t recv_crc;

            s_proto_frame[s_proto_index++] = ch;
            frame_len = (uint8_t)(ESP32_COMM_PROTO_OVERHEAD + s_proto_payload_len);
            calc_crc = esp32_comm_crc16_ibm(s_proto_frame, (uint16_t)(frame_len - 2U));
            recv_crc = (uint16_t)((uint16_t)s_proto_frame[frame_len - 2U] |
                                  ((uint16_t)s_proto_frame[frame_len - 1U] << 8));

            if(calc_crc == recv_crc)
            {
                if(pkt != 0)
                {
                    memset(pkt, 0, sizeof(*pkt));
                    pkt->proto_type = s_proto_frame[2];
                    pkt->proto_cmd = s_proto_frame[3];
                    pkt->proto_seq = s_proto_frame[4];
                    pkt->payload_len = s_proto_payload_len;
                    if(s_proto_payload_len > 0U)
                    {
                        memcpy(pkt->payload, &s_proto_frame[6], s_proto_payload_len);
                    }
                }
                esp32_comm_reset_parser();
                return 1U;
            }

            ESP32_Comm_SendProtoNack(s_proto_cmd, s_proto_seq, ESP32_COMM_ERR_BAD_CRC);
            esp32_comm_reset_parser();
            break;
        }

        default:
            esp32_comm_reset_parser();
            break;
    }

    return 0U;
}

void ESP32_Comm_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    s_rx_head = 0U;
    s_rx_tail = 0U;
    esp32_comm_reset_parser();

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

void ESP32_Comm_FlushRx(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    esp32_comm_reset_parser();
}

uint8_t ESP32_Comm_ReadPacket(ESP32_Comm_Packet_t *out_pkt)
{
    char ch = 0;

    if(out_pkt == 0)
        return 0U;

    while(esp32_comm_has_data() != 0U)
    {
        if(esp32_comm_read_byte(&ch) == 0U)
            break;
        if(esp32_comm_parse_byte((uint8_t)ch, out_pkt) != 0U)
            return 1U;
    }

    return 0U;
}

void ESP32_Comm_SendProtoAck(uint8_t cmd, uint8_t seq)
{
    esp32_comm_send_frame(ESP32_COMM_PROTO_TYPE_ACK, cmd, seq, 0, 0U);
}

void ESP32_Comm_SendProtoNack(uint8_t cmd, uint8_t seq, uint8_t err_code)
{
    esp32_comm_send_frame(ESP32_COMM_PROTO_TYPE_NACK, cmd, seq, &err_code, 1U);
}

void ESP32_Comm_SendProtoState(uint8_t proto_type,
                               uint8_t cmd,
                               uint8_t seq,
                               uint8_t stage,
                               uint8_t detail,
                               uint16_t flags,
                               int32_t weight_g)
{
    uint8_t payload[8];
    uint32_t weight_raw = (uint32_t)weight_g;

    payload[0] = stage;
    payload[1] = detail;
    payload[2] = (uint8_t)(flags & 0xFFU);
    payload[3] = (uint8_t)((flags >> 8) & 0xFFU);
    payload[4] = (uint8_t)(weight_raw & 0xFFU);
    payload[5] = (uint8_t)((weight_raw >> 8) & 0xFFU);
    payload[6] = (uint8_t)((weight_raw >> 16) & 0xFFU);
    payload[7] = (uint8_t)((weight_raw >> 24) & 0xFFU);

    esp32_comm_send_frame(proto_type, cmd, seq, payload, (uint8_t)sizeof(payload));
}

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
