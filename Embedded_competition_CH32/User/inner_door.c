/*********************************************************************
 * @file    inner_door.c
 * @brief   内门 SG90 舵机驱动模块
 * @note    使用 TIM3_CH1(PA6) 输出 50Hz PWM，控制 SG90 转动角度
 *********************************************************************/
#include "inner_door.h"

#define SERVO_TIMER_TICK_HZ         1000000UL
#define SERVO_PERIOD_US             20000U
#define SERVO_MIN_PULSE_US          500U
#define SERVO_MAX_PULSE_US          2500U
#define SERVO_DEFAULT_PULSE_US      500U

static uint16_t InnerDoor_AngleToPulseUs(uint8_t angle)
{
    if(angle > 180)
    {
        angle = 180;
    }

    return (uint16_t)(SERVO_MIN_PULSE_US +
                     ((uint32_t)(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle) / 180U);
}

void InnerDoor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(INNER_DOOR_SERVO_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(INNER_DOOR_SERVO_TIM_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = INNER_DOOR_SERVO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(INNER_DOOR_SERVO_PORT, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period = SERVO_PERIOD_US - 1;
    TIM_TimeBaseInitStructure.TIM_Prescaler = (SystemCoreClock / SERVO_TIMER_TICK_HZ) - 1;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(INNER_DOOR_SERVO_TIM, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = SERVO_DEFAULT_PULSE_US;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(INNER_DOOR_SERVO_TIM, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(INNER_DOOR_SERVO_TIM, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(INNER_DOOR_SERVO_TIM, ENABLE);
    TIM_Cmd(INNER_DOOR_SERVO_TIM, ENABLE);

    InnerDoor_Close();
    printf("InnerDoor Init OK. default close angle=%d\r\n", INNER_DOOR_ANGLE_CLOSE);
}

void InnerDoor_SetAngle(uint8_t angle)
{
    uint16_t pulse_us = InnerDoor_AngleToPulseUs(angle);
    TIM_SetCompare1(INNER_DOOR_SERVO_TIM, pulse_us);
    printf("InnerDoor Angle -> %d deg, pulse = %d us\r\n", angle, pulse_us);
}

void InnerDoor_Open(void)
{
    printf("InnerDoor Opening...\r\n");
    InnerDoor_SetAngle(INNER_DOOR_ANGLE_OPEN);
}

void InnerDoor_Close(void)
{
    printf("InnerDoor Closing...\r\n");
    InnerDoor_SetAngle(INNER_DOOR_ANGLE_CLOSE);
}
