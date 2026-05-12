#include "ch32v20x_gpio.h"
#include "TMC2209.h"
/*********************************************************************
 * @fn      TMC2209_Init
 * @brief   初始化 TMC2209 相关的 GPIO 引脚
 *********************************************************************/
void TMC2209_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 1. 开启 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 配置 EN, STEP, DIR 为推挽输出
    GPIO_InitStructure.GPIO_Pin = TMC_EN_PIN | TMC_STEP_PIN | TMC_DIR_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TMC_PORT, &GPIO_InitStructure);

    // 3. 初始状态：禁用电机(EN拉高)，脉冲和方向清零
    TMC2209_Disable();
    GPIO_ResetBits(TMC_PORT, TMC_STEP_PIN);
    GPIO_ResetBits(TMC_PORT, TMC_DIR_PIN);
}

/*********************************************************************
 * @fn      TMC2209_Enable
 * @brief   使能电机 (注意：TMC2209的EN引脚是低电平有效！)
 *********************************************************************/
void TMC2209_Enable(void)
{
    GPIO_ResetBits(TMC_PORT, TMC_EN_PIN); 
}

/*********************************************************************
 * @fn      TMC2209_Disable
 * @brief   失能电机 (拉高EN，此时用手可以轻松拧动电机轴)
 *********************************************************************/
void TMC2209_Disable(void)
{
    GPIO_SetBits(TMC_PORT, TMC_EN_PIN);   
}

/*********************************************************************
 * @fn      TMC2209_SetDir
 * @brief   设置电机旋转方向
 * @param   dir - 传入 DIR_OPEN 或 DIR_CLOSE
 *********************************************************************/
void TMC2209_SetDir(uint8_t dir)
{
    if(dir == DIR_OPEN) {
        GPIO_SetBits(TMC_PORT, TMC_DIR_PIN);
    } else {
        GPIO_ResetBits(TMC_PORT, TMC_DIR_PIN);
    }
}

/*********************************************************************
 * @fn      TMC2209_MoveSteps
 * @brief   让步进电机走指定的步数
 * @param   steps    - 走的步数 (如3200步)
 * @param   speed_us - 脉冲间隔的微秒数(us)。数值越小，电机转得越快！
 *********************************************************************/
void TMC2209_MoveSteps(uint32_t steps, uint16_t speed_us)
{
    for(uint32_t i = 0; i < steps; i++)
    {
        // 产生一个高电平脉冲
        GPIO_SetBits(TMC_PORT, TMC_STEP_PIN);
        Delay_Us(speed_us); 
        
        // 恢复低电平
        GPIO_ResetBits(TMC_PORT, TMC_STEP_PIN);
        Delay_Us(speed_us); 
        
        // 这一个完整的“高-低”周期，电机就会前进一个小步距角
    }
}

