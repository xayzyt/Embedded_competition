/*********************************************************************
 * @file    hx711.c
 * @brief   HX711 称重传感器底层实现（带超时保护）
 * @note    修复原先 DT 一直为高时死等导致主流程卡死的问题
 *********************************************************************/
#include "hx711.h"

// 偏移量 (存放去皮后的零点原始值)
int32_t HX711_Offset = 0;

// =========================================================================
// 【校准核心区】：调整这里的数字，消除那 ±10 克的误差！
// 规则：
// 1. 如果显示的重量 比实际重 -> 把数字改大
// 2. 如果显示的重量 比实际轻 -> 把数字改小
// =========================================================================
float HX711_Scale = 427.0f;

/* ====== 超时与采样参数 ====== */
#define HX711_READ_READY_TIMEOUT_MS    300U
#define HX711_TARE_READY_TIMEOUT_MS     50U
#define HX711_TARE_SAMPLE_COUNT          8U
#define HX711_TARE_MAX_ATTEMPTS         12U

/*********************************************************************
 * @fn      HX711_Init
 * @brief   初始化 HX711 的 GPIO
 *********************************************************************/
void HX711_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 1. 开启 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // 2. SCK 配置为推挽输出 (单片机发送时钟)
    GPIO_InitStructure.GPIO_Pin = HX711_SCK_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(HX711_PORT, &GPIO_InitStructure);

    // 3. DT 配置为上拉输入 (单片机接收数据)
    GPIO_InitStructure.GPIO_Pin = HX711_DT_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(HX711_PORT, &GPIO_InitStructure);

    // SCK 初始拉低，处于空闲状态
    GPIO_ResetBits(HX711_PORT, HX711_SCK_PIN);
}

/*********************************************************************
 * @fn      HX711_Is_Ready
 * @brief   判断 HX711 是否已经准备好数据
 * @return  1: 已准备好   0: 未准备好
 *********************************************************************/
uint8_t HX711_Is_Ready(void)
{
    return (GPIO_ReadInputDataBit(HX711_PORT, HX711_DT_PIN) == Bit_RESET) ? 1 : 0;
}

/*********************************************************************
 * @fn      HX711_WaitReady
 * @brief   带超时等待 HX711 就绪
 * @param   timeout_ms 超时时间（毫秒）
 * @return  1: 成功就绪   0: 超时
 *********************************************************************/
uint8_t HX711_WaitReady(uint32_t timeout_ms)
{
    while(timeout_ms--)
    {
        if(HX711_Is_Ready())
        {
            return 1;
        }
        Delay_Ms(1);
    }
    return 0;
}

/*********************************************************************
 * @fn      HX711_Read_Unsafe
 * @brief   读取 HX711 的 24 位原始数据（调用前必须确认 Ready）
 * @return  24位有符号整数
 *********************************************************************/
static int32_t HX711_Read_Unsafe(void)
{
    uint32_t count = 0;
    uint8_t i;

    GPIO_ResetBits(HX711_PORT, HX711_SCK_PIN);

    for(i = 0; i < 24; i++)
    {
        GPIO_SetBits(HX711_PORT, HX711_SCK_PIN);
        count = count << 1;
        Delay_Us(1);

        GPIO_ResetBits(HX711_PORT, HX711_SCK_PIN);

        if(GPIO_ReadInputDataBit(HX711_PORT, HX711_DT_PIN) == Bit_SET)
        {
            count++;
        }
        Delay_Us(1);
    }

    // 第 25 个脉冲，设置增益为 128
    GPIO_SetBits(HX711_PORT, HX711_SCK_PIN);
    Delay_Us(1);
    GPIO_ResetBits(HX711_PORT, HX711_SCK_PIN);
    Delay_Us(1);

    // 符号位处理
    if(count & 0x00800000)
    {
        count |= 0xFF000000;
    }
    else
    {
        count &= 0x00FFFFFF;
    }

    return (int32_t)count;
}

/*********************************************************************
 * @fn      HX711_Read
 * @brief   读取 HX711 的 24 位原始数据（带超时保护）
 * @return  24位有符号整数；若超时，返回当前 Offset，避免主流程卡死
 *********************************************************************/
int32_t HX711_Read(void)
{
    if(!HX711_WaitReady(HX711_READ_READY_TIMEOUT_MS))
    {
        printf("HX711 Read Timeout!\r\n");
        return HX711_Offset;
    }

    return HX711_Read_Unsafe();
}

/*********************************************************************
 * @fn      HX711_Tare
 * @brief   去皮重（带失败保护，不再无限阻塞）
 * @return  1: 去皮成功   0: 去皮失败
 *********************************************************************/
uint8_t HX711_Tare(void)
{
    int64_t sum = 0;
    uint8_t ok_cnt = 0;
    uint8_t attempt = 0;

    for(attempt = 0; attempt < HX711_TARE_MAX_ATTEMPTS && ok_cnt < HX711_TARE_SAMPLE_COUNT; attempt++)
    {
        if(HX711_WaitReady(HX711_TARE_READY_TIMEOUT_MS))
        {
            sum += HX711_Read_Unsafe();
            ok_cnt++;
        }
        Delay_Ms(10);
    }

    if(ok_cnt == 0)
    {
        printf("HX711 Tare Failed!\r\n");
        return 0;
    }

    HX711_Offset = (int32_t)(sum / ok_cnt);
    printf("HX711 Tare Done! Offset = %d (sample=%u)\r\n", (int)HX711_Offset, ok_cnt);
    return 1;
}

/*********************************************************************
 * @fn      HX711_Get_Weight
 * @brief   获取真实的物品重量
 * @return  计算后的重量 (克)
 *********************************************************************/
int32_t HX711_Get_Weight(void)
{
    int32_t current_val = 0;
    int32_t weight = 0;

    current_val = HX711_Read();

    // 软件修正极性：不改焊线，直接在软件里翻转正负方向
    weight = (int32_t)((HX711_Offset - current_val) / HX711_Scale);

    return weight;
}
