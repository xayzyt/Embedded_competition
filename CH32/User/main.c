#include "debug.h"
#include "ch32_app.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    USART_Printf_Init(115200);
    CH32_App_Init();
  

    while(1)
    {
        
        CH32_App_RunOnce();
    }
}
