#pragma once
#include "esp_err.h"
#include "app_ch32_link.h"
// 控制器公共接口：把 UI、任务、视觉和 CH32 串成自动对接主循环。

#ifdef __cplusplus
extern "C" {
#endif
// 初始化控制器状态、默认目标和任务回调。
esp_err_t app_ctrl_init(void);
// 启动控制主循环任务。
esp_err_t app_ctrl_start(void);
// CH32 接收回调，解析后的从控消息从这里进入控制状态机。
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx);
#ifdef __cplusplus
}
#endif
