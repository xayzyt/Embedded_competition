#include "ch32v20x_gpio.h"
#include "L298N.h"
/**************************
初始化推杆控制的 GPIO 引脚
**************************/
void PushRod_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 1. 开启 GPIOA 的时钟 
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 配置 PA4 和 PA5 为推挽输出模式 
    GPIO_InitStructure.GPIO_Pin = PUSHROD_IN1_PIN | PUSHROD_IN2_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PUSHROD_PORT, &GPIO_InitStructure);

    // 3. 初始状态设置为全低电平 (确保上电时推杆不动)
    GPIO_ResetBits(PUSHROD_PORT, PUSHROD_IN1_PIN | PUSHROD_IN2_PIN);
}

/**************************
推杆伸出 (正转)
**************************/
void PushRod_Extend(void)
{
    printf("PushRod Extending...\r\n");
    GPIO_SetBits(PUSHROD_PORT, PUSHROD_IN1_PIN);   // IN1 = 1 (高电平)
    GPIO_ResetBits(PUSHROD_PORT, PUSHROD_IN2_PIN); // IN2 = 0 (低电平)
}
/**************************
推杆缩回 (反转)
**************************/
void PushRod_Retract(void)
{
    printf("PushRod Retracting...\r\n");
    GPIO_ResetBits(PUSHROD_PORT, PUSHROD_IN1_PIN); // IN1 = 0 (低电平)
    GPIO_SetBits(PUSHROD_PORT, PUSHROD_IN2_PIN);   // IN2 = 1 (高电平)
}
/**************************
推杆停止 (刹车)
**************************/
void PushRod_Stop(void)
{
    printf("PushRod Stopped.\r\n");
    // IN1 = 0, IN2 = 0 (断电刹车)
    GPIO_ResetBits(PUSHROD_PORT, PUSHROD_IN1_PIN | PUSHROD_IN2_PIN); 
}
