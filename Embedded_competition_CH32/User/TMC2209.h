#ifndef __TMC2209_H
#define __TMC2209_H

#include "debug.h"

/* ========== 引脚映射配置 ========== */
#define TMC_PORT      GPIOA
#define TMC_EN_PIN    GPIO_Pin_0    // 使能引脚 (低电平有效)
#define TMC_STEP_PIN  GPIO_Pin_1    // 脉冲引脚
#define TMC_DIR_PIN   GPIO_Pin_2    // 方向引脚

/* ========== 运动方向宏定义 ========== */
#define DIR_OPEN      0             // 开门方向 
#define DIR_CLOSE     1             // 关门方向

void TMC2209_Init(void);                      // 初始化引脚
void TMC2209_Enable(void);                    // 开启电机 (锁住转轴准备接收脉冲)
void TMC2209_Disable(void);                   // 关闭电机 (转轴可自由转动，防发热)
void TMC2209_SetDir(uint8_t dir);             // 设置转动方向
void TMC2209_MoveSteps(uint32_t steps, uint16_t speed_us); // 发送脉冲移动指定步数

#endif
