#include "esp32_com.h"

#include <string.h>
#include <stdio.h>

/*
 * ============================================================================
 * 文件说明
 * ----------------------------------------------------------------------------
 * 这个文件是 CH32 <-> ESP32 之间的串口通信协议层。
 *
 * 它主要负责三件事：
 * 1. 初始化 USART1，并通过中断接收字节流
 * 2. 将接收到的字节放入 FIFO 缓冲区
 * 3. 从 FIFO 中解析出两类上层可用的“命令包”：
 *    - 老协议：ASCII 文本，例如 @A\n
 *    - 新协议：二进制帧，带帧头、版本、命令字、长度、CRC
 *
 * 对外提供的接口非常关键：
 * - ESP32_Comm_Init()           : 初始化串口与解析状态
 * - ESP32_Comm_ReadPacket()     : 从 FIFO 里提取一包完整命令
 * - ESP32_Comm_SendProtoAck()   : 发送新协议 ACK
 * - ESP32_Comm_SendProtoState() : 发送新协议状态帧
 * - ESP32_Comm_SendLegacyXxx()  : 发送老协议调试文本
 *
 * 设计思想：
 * - 串口中断只做“收字节入队”，尽量轻量
 * - 真正的协议解析在主循环中做，避免中断过重
 * - 同时兼容新旧两套协议，便于调试与迁移
 * ============================================================================
 */

/*
 * =============================
 * 接收 FIFO 相关状态
 * =============================
 *
 * s_rx_head : 写指针（中断接收字节后向前推进）
 * s_rx_tail : 读指针（主循环消费字节后向前推进）
 * s_rx_fifo : 环形缓冲区本体
 *
 * 为什么要用 FIFO？
 * 因为串口字节到达是异步的，中断里先快速接住，
 * 主循环再慢慢解析，避免丢字节。
 */
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static volatile uint8_t s_rx_fifo[ESP32_COMM_RX_FIFO_SIZE];

/*
 * =============================
 * 老协议解析状态机变量
 * =============================
 * 老协议格式非常简单：
 *
 *   @A\n
 *   @W\n
 *   @S\n
 *
 * 即：
 * - '@' 作为起始符
 * - 一个大写字母作为命令字
 * - '\n' 作为结束符
 *
 * s_legacy_state 当前状态机所处阶段：
 * - 0：等起始符 '@'
 * - 1：等命令字母
 * - 2：等结束符 '\n'
 */
static uint8_t s_legacy_state = 0;
static char s_legacy_cmd = 0;

/*
 * =============================
 * 新协议解析状态机变量
 * =============================
 * 新协议格式大致为：
 *
 * [SOF1][SOF2][VER][TYPE][CMD][SEQ][LEN][PAYLOAD...][CRC_L][CRC_H]
 *
 * 解析过程需要逐字节推进，因此这里保留一组状态机上下文。
 */
static uint8_t s_proto_state = 0;     /* 当前二进制协议状态机所处状态 */
static uint8_t s_proto_buf[64];       /* 暂存当前正在拼接的一帧 */
static uint8_t s_proto_index = 0;     /* 当前已经写入了多少字节 */
static uint8_t s_proto_len = 0;       /* 当前帧 payload 长度 */
static uint8_t s_proto_cmd = 0;       /* 当前帧 cmd，便于出错时回 NACK */
static uint8_t s_proto_seq = 0;       /* 当前帧 seq，便于出错时回 NACK */

/*
 * 发送单个字节。
 *
 * 流程：
 * 1. 将字节写入 USART1 数据寄存器
 * 2. 等待发送完成标志 TC 置位
 *
 * 注意：
 * 这是一个阻塞发送函数，适合当前这种低频控制协议。
 * 如果后续通信量变大，可以考虑：
 * - DMA 发送
 * - 发送 FIFO
 * - 中断发送
 */
static void esp32_comm_send_byte(uint8_t byte)
{
    USART_SendData(USART1, byte);
    while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
    {
        /* 阻塞等待当前字节真正发完 */
    }
}

/*
 * 发送一串字节数组。
 *
 * 参数：
 * - data : 要发送的数据首地址
 * - len  : 数据长度
 *
 * 如果 data == 0，直接保护返回。
 */
static void esp32_comm_send_bytes(const uint8_t *data, uint16_t len)
{
    uint16_t i = 0;

    if(data == 0)
    {
        return;
    }

    for(i = 0; i < len; i++)
    {
        esp32_comm_send_byte(data[i]);
    }
}

/*
 * 计算 CRC16-IBM。
 *
 * 初始值：0xFFFF
 * 多项式：0xA001（右移版写法）
 *
 * 这个 CRC 用于新协议帧完整性校验，
 * 防止串口干扰导致帧内容损坏而被误执行。
 */
static uint16_t esp32_comm_crc16_ibm(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i = 0;
    uint8_t bit = 0;

    for(i = 0; i < len; i++)
    {
        crc ^= data[i];
        for(bit = 0; bit < 8U; bit++)
        {
            if((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/*
 * 往接收 FIFO 中压入一个字节。
 *
 * 这是环形缓冲区写入逻辑：
 * - next 为“head 的下一个位置”
 * - 如果 next == tail，说明 FIFO 满了
 *
 * 当前策略：
 * - FIFO 满时，丢弃最旧的数据（tail 向前挪一格）
 * - 再把新字节写入
 *
 * 这种策略的优点是：
 * - 总能保住最近收到的数据
 *
 * 缺点是：
 * - 极端情况下会丢老数据，导致某些帧解析失败
 */
static void esp32_comm_fifo_push(uint8_t ch)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % ESP32_COMM_RX_FIFO_SIZE);

    if(next == s_rx_tail)
    {
        /* FIFO 已满，主动覆盖最旧数据 */
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    }

    s_rx_fifo[s_rx_head] = ch;
    s_rx_head = next;
}

/*
 * 尝试按“老协议”解析一个字节。
 *
 * 这是一个典型的有限状态机：
 *
 * 状态 0：等待 '@'
 * 状态 1：等待 A~Z 的命令字符
 * 状态 2：等待 '\n' 结束
 *
 * 若成功拼出一帧，就填充 pkt，并返回 1。
 * 否则返回 0，表示“还没拼成完整帧 / 当前字节不匹配”。
 */
static uint8_t esp32_comm_try_parse_legacy(uint8_t ch, ESP32_Comm_Packet_t *pkt)
{
    switch(s_legacy_state)
    {
        case 0:
            /* 初始态：只有遇到 '@' 才认为一帧开始。 */
            if(ch == '@')
            {
                s_legacy_state = 1;
            }
            break;

        case 1:
            /* 第二态：要求命令字符必须是 A~Z 大写字母。 */
            if((ch >= 'A') && (ch <= 'Z'))
            {
                s_legacy_cmd = (char)ch;
                s_legacy_state = 2;
            }
            else if(ch == '@')
            {
                /*
                 * 如果连续遇到 '@'，说明可能上一个起始符是噪声，
                 * 这里直接把当前 '@' 视为新的起点。
                 */
                s_legacy_state = 1;
            }
            else
            {
                /* 非法字符，状态机复位。 */
                s_legacy_state = 0;
                s_legacy_cmd = 0;
            }
            break;

        case 2:
            /* 第三态：等待 '\n' 作为帧结束。 */
            if(ch == '\n')
            {
                memset(pkt, 0, sizeof(*pkt));
                pkt->is_proto = 0U;
                pkt->legacy_cmd = s_legacy_cmd;
                s_legacy_state = 0;
                s_legacy_cmd = 0;
                return 1U;
            }
            else if(ch == '@')
            {
                /*
                 * 如果等待结束符时又来了 '@'，
                 * 说明上一个帧尾丢了，这里重新开始寻找新帧。
                 */
                s_legacy_state = 1;
                s_legacy_cmd = 0;
            }
            else
            {
                /* 其它字符均视为非法，直接丢弃当前半帧。 */
                s_legacy_state = 0;
                s_legacy_cmd = 0;
            }
            break;

        default:
            /* 理论上不会到这里；为了保险，统一复位。 */
            s_legacy_state = 0;
            s_legacy_cmd = 0;
            break;
    }

    return 0U;
}

/*
 * 尝试按“新二进制协议”解析一个字节。
 *
 * 这也是一个字节流状态机。
 * 状态推进顺序：
 *
 * 0: 等 SOF1
 * 1: 等 SOF2
 * 2: 等 VER
 * 3: 收 TYPE
 * 4: 收 CMD
 * 5: 收 SEQ
 * 6: 收 LEN
 * 7: 收 PAYLOAD
 * 8: 收 CRC 低字节
 * 9: 收 CRC 高字节并校验
 *
 * 成功则填充 out pkt，返回 1。
 * 失败则复位状态机，并在必要时发 NACK / 错误文本。
 */
static uint8_t esp32_comm_try_parse_proto(uint8_t ch, ESP32_Comm_Packet_t *pkt)
{
    uint16_t calc_crc = 0;
    uint16_t recv_crc = 0;
    uint8_t frame_len = 0;

    switch(s_proto_state)
    {
        case 0:
            /* 等待第一个帧头字节 SOF1。 */
            if(ch == ESP32_COMM_PROTO_SOF1)
            {
                s_proto_state = 1;
                s_proto_buf[0] = ch;
                s_proto_index = 1U;
                s_proto_len = 0U;
                s_proto_cmd = 0U;
                s_proto_seq = 0U;
            }
            break;

        case 1:
            /* 等待第二个帧头字节 SOF2。 */
            if(ch == ESP32_COMM_PROTO_SOF2)
            {
                s_proto_buf[s_proto_index++] = ch;
                s_proto_state = 2;
            }
            else if(ch == ESP32_COMM_PROTO_SOF1)
            {
                /*
                 * 遇到连续 SOF1 时，认为当前字节可能是新一帧的起点，
                 * 所以不彻底清空，而是保留它重新开始。
                 */
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
            /* 检查协议版本是否匹配。 */
            if(ch == ESP32_COMM_PROTO_VER)
            {
                s_proto_buf[s_proto_index++] = ch;
                s_proto_state = 3;
            }
            else
            {
                s_proto_state = 0;
                s_proto_index = 0U;
            }
            break;

        case 3:
            /* 收 TYPE 字段。 */
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 4;
            break;

        case 4:
            /* 收 CMD 字段，并缓存下来，方便后续错误时发 NACK。 */
            s_proto_cmd = ch;
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 5;
            break;

        case 5:
            /* 收 SEQ 字段，同样缓存下来。 */
            s_proto_seq = ch;
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 6;
            break;

        case 6:
            /*
             * 收 LEN 字段。
             * 这里必须做长度上限保护，避免恶意或异常帧导致缓存越界。
             */
            if(ch > ESP32_COMM_PROTO_MAX_PAYLOAD)
            {
                s_proto_state = 0;
                s_proto_index = 0U;
                ESP32_Comm_SendProtoNack(s_proto_cmd, s_proto_seq, ESP32_COMM_ERR_INTERNAL);
                ESP32_Comm_SendLegacyError("BAD_LEN");
                break;
            }
            s_proto_len = ch;
            s_proto_buf[s_proto_index++] = ch;
            if(s_proto_len == 0U)
            {
                /* 没有 payload，直接进入 CRC 读取阶段。 */
                s_proto_state = 8;
            }
            else
            {
                s_proto_state = 7;
            }
            break;

        case 7:
            /* 持续收 payload，直到达到声明的长度。 */
            s_proto_buf[s_proto_index++] = ch;
            if(s_proto_index >= (uint8_t)(7U + s_proto_len))
            {
                s_proto_state = 8;
            }
            break;

        case 8:
            /* 收 CRC 低字节。 */
            s_proto_buf[s_proto_index++] = ch;
            s_proto_state = 9;
            break;

        case 9:
            /* 收 CRC 高字节，并执行整帧 CRC 校验。 */
            s_proto_buf[s_proto_index++] = ch;
            frame_len = (uint8_t)(7U + s_proto_len + 2U);
            calc_crc = esp32_comm_crc16_ibm(s_proto_buf, (uint16_t)(frame_len - 2U));
            recv_crc = (uint16_t)((uint16_t)s_proto_buf[frame_len - 2U] |
                                  ((uint16_t)s_proto_buf[frame_len - 1U] << 8));

            if(calc_crc == recv_crc)
            {
                /* CRC 通过，组装成上层可用的 packet 结构。 */
                memset(pkt, 0, sizeof(*pkt));
                pkt->is_proto = 1U;
                pkt->proto_type = s_proto_buf[3];
                pkt->proto_cmd = s_proto_buf[4];
                pkt->proto_seq = s_proto_buf[5];
                pkt->payload_len = s_proto_len;
                if(s_proto_len > 0U)
                {
                    memcpy(pkt->payload, &s_proto_buf[7], s_proto_len);
                }

                /* 解析成功后要把状态机复位，为下一帧做准备。 */
                s_proto_state = 0;
                s_proto_index = 0U;
                s_proto_len = 0U;
                return 1U;
            }
            else
            {
                /* CRC 失败，通知上位。 */
                ESP32_Comm_SendProtoNack(s_proto_cmd, s_proto_seq, ESP32_COMM_ERR_BAD_CRC);
                ESP32_Comm_SendLegacyError("BAD_CRC");
            }

            s_proto_state = 0;
            s_proto_index = 0U;
            s_proto_len = 0U;
            break;

        default:
            /* 异常状态统一清空复位。 */
            s_proto_state = 0;
            s_proto_index = 0U;
            s_proto_len = 0U;
            break;
    }

    return 0U;
}

/*
 * 发送一行带前缀的文本。
 *
 * 常见格式：
 * - [ACK] X\r\n
 * - [STS] READY\r\n
 * - [ERR] BAD_CRC\r\n
 *
 * prefix / text 允许为空指针，函数内部都做了保护。
 */
static void esp32_comm_send_text_prefixed(const char *prefix, const char *text)
{
    if(prefix != 0)
    {
        esp32_comm_send_bytes((const uint8_t *)prefix, (uint16_t)strlen(prefix));
    }

    if(text != 0)
    {
        esp32_comm_send_bytes((const uint8_t *)text, (uint16_t)strlen(text));
    }

    /* 所有文本输出统一以 CRLF 结尾。 */
    esp32_comm_send_bytes((const uint8_t *)"\r\n", 2U);
}

/*
 * 初始化 ESP32 通信接口。
 *
 * 硬件资源：
 * - USART1
 * - PA9  -> TX
 * - PA10 -> RX
 *
 * 初始化内容：
 * 1. 清空 FIFO 与解析状态机
 * 2. 配置 GPIO
 * 3. 配置 USART1 = 115200 8N1
 * 4. 开启 RXNE 接收中断
 * 5. 打开 USART1 外设
 */
void ESP32_Comm_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* 先把所有软件状态清零。 */
    s_rx_head = 0;
    s_rx_tail = 0;
    s_legacy_state = 0U;
    s_legacy_cmd = 0;
    s_proto_state = 0U;
    s_proto_index = 0U;
    s_proto_len = 0U;
    s_proto_cmd = 0U;
    s_proto_seq = 0U;

    /* 打开 GPIOA 和 USART1 时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    /* PA9: USART1_TX，复用推挽输出。 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA10: USART1_RX，上拉输入。 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 配置 115200, 8 数据位, 1 停止位, 无校验, 收发都开。 */
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    /* 清除可能残留的接收标志，避免上电误进中断。 */
    USART_ClearFlag(USART1, USART_FLAG_RXNE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* 配置 USART1 中断优先级。 */
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 最后再真正使能串口。 */
    USART_Cmd(USART1, ENABLE);
}

/*
 * 判断接收 FIFO 中是否有数据。
 *
 * head != tail 说明至少还有一个字节未被主循环消费。
 */
uint8_t ESP32_Comm_HasData(void)
{
    return (s_rx_head != s_rx_tail) ? 1U : 0U;
}

/*
 * 从接收 FIFO 中读取一个字节。
 *
 * 返回值：
 * - 1：成功读到一个字节
 * - 0：FIFO 为空，或 out_ch == 0
 */
uint8_t ESP32_Comm_ReadByte(char *out_ch)
{
    if((out_ch == 0) || (s_rx_head == s_rx_tail))
    {
        return 0U;
    }

    *out_ch = (char)s_rx_fifo[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % ESP32_COMM_RX_FIFO_SIZE);
    return 1U;
}

/*
 * 清空接收 FIFO 以及两套协议解析状态机。
 *
 * 适用于：
 * - 某个动作被中止后，希望丢弃历史残留字节
 * - 通信异常恢复
 */
void ESP32_Comm_FlushRx(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_legacy_state = 0U;
    s_legacy_cmd = 0;
    s_proto_state = 0U;
    s_proto_index = 0U;
    s_proto_len = 0U;
    s_proto_cmd = 0U;
    s_proto_seq = 0U;
}

/*
 * 兼容旧接口：读取一个老协议命令字符。
 *
 * 这个函数内部实际仍然调用 ReadPacket()，
 * 只是只在解析出“老协议 packet”时才返回成功。
 */
uint8_t ESP32_Comm_ReadFrameCmd(char *out_cmd)
{
    ESP32_Comm_Packet_t pkt;

    if(out_cmd == 0)
    {
        return 0U;
    }

    while(ESP32_Comm_ReadPacket(&pkt))
    {
        if(pkt.is_proto == 0U)
        {
            *out_cmd = pkt.legacy_cmd;
            return 1U;
        }
    }

    return 0U;
}

/*
 * 从接收 FIFO 中尝试提取一包完整命令。
 *
 * 解析策略：
 * 1. 不断取字节
 * 2. 先喂给新协议解析器
 * 3. 如果当前字节是 '\r'，跳过不喂给老协议
 * 4. 再喂给老协议解析器
 *
 * 为什么优先新协议？
 * - 因为新协议帧头和内容是二进制的，误判代价更高
 * - 老协议更简单，放在后面兼容即可
 */
uint8_t ESP32_Comm_ReadPacket(ESP32_Comm_Packet_t *out_pkt)
{
    char ch = 0;

    if(out_pkt == 0)
    {
        return 0U;
    }

    while(ESP32_Comm_ReadByte(&ch))
    {
        if(esp32_comm_try_parse_proto((uint8_t)ch, out_pkt))
        {
            return 1U;
        }

        /*
         * 老协议只关心 '@'、命令字母、'\n'。
         * CR 统一忽略，这样可兼容 \r\n 风格串口发送。
         */
        if(ch == '\r')
        {
            continue;
        }

        if(esp32_comm_try_parse_legacy((uint8_t)ch, out_pkt))
        {
            return 1U;
        }
    }

    return 0U;
}

/*
 * 发送老协议 ACK 文本。
 *
 * 例如 cmd='A' 时，输出：
 * [ACK] A\r\n
 */
void ESP32_Comm_SendLegacyAck(char cmd)
{
    char line[16];

    line[0] = cmd;
    line[1] = '\0';
    esp32_comm_send_text_prefixed("[ACK] ", line);
}

/* 发送老协议状态文本，例如：[STS] CH32_READY */
void ESP32_Comm_SendLegacyStatus(const char *text)
{
    esp32_comm_send_text_prefixed("[STS] ", text);
}

/* 发送老协议错误文本，例如：[ERR] BAD_CRC */
void ESP32_Comm_SendLegacyError(const char *text)
{
    esp32_comm_send_text_prefixed("[ERR] ", text);
}

/* 发送老协议调试文本，例如：[DBG] WEIGHT=36 */
void ESP32_Comm_SendLegacyDebug(const char *text)
{
    esp32_comm_send_text_prefixed("[DBG] ", text);
}

/*
 * 发送新协议 ACK 帧。
 *
 * 帧格式：
 * [SOF1][SOF2][VER][TYPE=ACK][CMD][SEQ][LEN=0][CRC_L][CRC_H]
 */
void ESP32_Comm_SendProtoAck(uint8_t cmd, uint8_t seq)
{
    uint8_t frame[9];
    uint16_t crc = 0;

    frame[0] = ESP32_COMM_PROTO_SOF1;
    frame[1] = ESP32_COMM_PROTO_SOF2;
    frame[2] = ESP32_COMM_PROTO_VER;
    frame[3] = ESP32_COMM_PROTO_TYPE_ACK;
    frame[4] = cmd;
    frame[5] = seq;
    frame[6] = 0U;
    crc = esp32_comm_crc16_ibm(frame, 7U);
    frame[7] = (uint8_t)(crc & 0xFFU);
    frame[8] = (uint8_t)((crc >> 8) & 0xFFU);
    esp32_comm_send_bytes(frame, 9U);
}

/*
 * 发送新协议 NACK 帧。
 *
 * payload 长度固定为 1 字节，内容就是 err_code。
 */
void ESP32_Comm_SendProtoNack(uint8_t cmd, uint8_t seq, uint8_t err_code)
{
    uint8_t frame[10];
    uint16_t crc = 0;

    frame[0] = ESP32_COMM_PROTO_SOF1;
    frame[1] = ESP32_COMM_PROTO_SOF2;
    frame[2] = ESP32_COMM_PROTO_VER;
    frame[3] = ESP32_COMM_PROTO_TYPE_NACK;
    frame[4] = cmd;
    frame[5] = seq;
    frame[6] = 1U;
    frame[7] = err_code;
    crc = esp32_comm_crc16_ibm(frame, 8U);
    frame[8] = (uint8_t)(crc & 0xFFU);
    frame[9] = (uint8_t)((crc >> 8) & 0xFFU);
    esp32_comm_send_bytes(frame, 10U);
}

/*
 * 发送新协议状态帧。
 *
 * payload 固定 8 字节：
 * byte0 : stage
 * byte1 : detail（通常是错误码或附加信息）
 * byte2 : flags L
 * byte3 : flags H
 * byte4~7 : weight_g 的 32 位整数表示（小端）
 *
 * 这样 ESP32 一次就能拿到：
 * - 当前阶段
 * - 当前错误码 / detail
 * - 状态位图
 * - 最近重量
 */
void ESP32_Comm_SendProtoState(uint8_t proto_type,
                               uint8_t cmd,
                               uint8_t seq,
                               uint8_t stage,
                               uint8_t detail,
                               uint16_t flags,
                               int32_t weight_g)
{
    uint8_t frame[17];
    uint16_t crc = 0;

    frame[0] = ESP32_COMM_PROTO_SOF1;
    frame[1] = ESP32_COMM_PROTO_SOF2;
    frame[2] = ESP32_COMM_PROTO_VER;
    frame[3] = proto_type;
    frame[4] = cmd;
    frame[5] = seq;
    frame[6] = 8U;
    frame[7] = stage;
    frame[8] = detail;
    frame[9] = (uint8_t)(flags & 0xFFU);
    frame[10] = (uint8_t)((flags >> 8) & 0xFFU);
    frame[11] = (uint8_t)((uint32_t)weight_g & 0xFFU);
    frame[12] = (uint8_t)(((uint32_t)weight_g >> 8) & 0xFFU);
    frame[13] = (uint8_t)(((uint32_t)weight_g >> 16) & 0xFFU);
    frame[14] = (uint8_t)(((uint32_t)weight_g >> 24) & 0xFFU);
    crc = esp32_comm_crc16_ibm(frame, 15U);
    frame[15] = (uint8_t)(crc & 0xFFU);
    frame[16] = (uint8_t)((crc >> 8) & 0xFFU);
    esp32_comm_send_bytes(frame, 17U);
}

/*
 * 声明 USART1 中断服务函数，并指定 WCH 快速中断属性。
 */
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*
 * USART1 接收中断处理函数。
 *
 * 设计原则：
 * - 中断里只做最轻量的工作：收 1 个字节，压入 FIFO
 * - 不在中断里做协议解析，不做 printf，不做复杂逻辑
 *
 * 这样可以尽量减少中断占用时间，提高系统实时性。
 */
void USART1_IRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART1);
        esp32_comm_fifo_push(ch);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
