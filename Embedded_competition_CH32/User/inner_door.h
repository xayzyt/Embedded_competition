#ifndef __INNER_DOOR_H
#define __INNER_DOOR_H

#include "debug.h"

/* ========== SG90 内门舵机引脚配置 ========== */
#define INNER_DOOR_SERVO_PORT       GPIOA
#define INNER_DOOR_SERVO_PIN        GPIO_Pin_6
#define INNER_DOOR_SERVO_GPIO_CLK   RCC_APB2Periph_GPIOA
#define INNER_DOOR_SERVO_TIM        TIM3
#define INNER_DOOR_SERVO_TIM_CLK    RCC_APB1Periph_TIM3

/* ========== 角度定义 ========== */
/* 当前现场机械方向：0 度 = 开门，90 度 = 关门 */
#define INNER_DOOR_ANGLE_CLOSE      90
#define INNER_DOOR_ANGLE_OPEN       0

void InnerDoor_Init(void);
void InnerDoor_SetAngle(uint8_t angle);
void InnerDoor_Open(void);
void InnerDoor_Close(void);

#endif
