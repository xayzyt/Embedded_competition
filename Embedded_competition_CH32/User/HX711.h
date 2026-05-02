#ifndef __HX711_H
#define __HX711_H

#include "debug.h"

// 按照我们之前的原理图，HX711 接在 PB10(SCK) 和 PB11(DT)
#define HX711_PORT      GPIOB
#define HX711_SCK_PIN   GPIO_Pin_10   // 时钟引脚
#define HX711_DT_PIN    GPIO_Pin_11   // 数据引脚

void    HX711_Init(void);                        // 初始化称重模块引脚
uint8_t HX711_Is_Ready(void);                    // 判断 HX711 是否准备好
uint8_t HX711_WaitReady(uint32_t timeout_ms);    // 带超时等待 HX711 就绪
int32_t HX711_Read(void);                        // 读取一次传感器原始 ADC 数据（带超时保护）
uint8_t HX711_Tare(void);                        // 去皮重（成功返回 1，失败返回 0）
int32_t HX711_Get_Weight(void);                  // 获取最终计算出的重量 (单位: 克)

#endif
