#ifndef __CH32_APP_H
#define __CH32_APP_H

// CH32 侧业务状态机入口。
//
// Init 完成 GPIO、执行器、称重和串口协议初始化；RunOnce 执行一次非抢占式
// 状态机轮询。main() 应持续调用 RunOnce，不能在外层加入长时间阻塞。

#ifdef __cplusplus
extern "C" {
#endif

// 初始化机械执行器、传感器和 ESP32 通信状态。
void CH32_App_Init(void);

// 推进一步业务状态机，并处理串口命令、超时和安全检查。
void CH32_App_RunOnce(void);

#ifdef __cplusplus
}
#endif

#endif
