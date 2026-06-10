#ifndef __L298N_H
#define __L298N_H

// L298N 推杆驱动接口。
// IN1/IN2 的组合决定伸出、收回或制动；调用方负责根据限位开关及时停止。
#define PUSHROD_PORT    GPIOA
#define PUSHROD_IN1_PIN GPIO_Pin_4
#define PUSHROD_IN2_PIN GPIO_Pin_5

// 配置推杆方向引脚，并将输出置为停止状态。
void PushRod_Init(void);

// 驱动推杆向外伸出；该函数不会等待运动完成。
void PushRod_Extend(void);

// 驱动推杆向内收回；该函数不会等待运动完成。
void PushRod_Retract(void);

// 停止推杆输出，供限位、超时或故障路径调用。
void PushRod_Stop(void);


#endif
