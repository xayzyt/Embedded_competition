#include "debug.h"
#include "ch32_app.h"

int main(void)
{
    // 设置中断优先级分组并初始化系统时钟相关的延时函数。
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    // 初始化调试串口，然后由业务模块完成执行器、传感器和协议初始化。
    USART_Printf_Init(115200);
    CH32_App_Init();

    // CH32 业务采用轮询状态机；单次调用应尽快返回。
    while(1)
    {
        CH32_App_RunOnce();
    }
}
