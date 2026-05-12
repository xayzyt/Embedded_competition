#ifndef __L298N_H
#define __L298N_H

#define PUSHROD_PORT    GPIOA
#define PUSHROD_IN1_PIN GPIO_Pin_4
#define PUSHROD_IN2_PIN GPIO_Pin_5

void PushRod_Init(void);

void PushRod_Extend(void);

void PushRod_Retract(void);

void PushRod_Stop(void);


#endif
