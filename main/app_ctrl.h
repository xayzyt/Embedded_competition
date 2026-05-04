#pragma once

/*
 * 接驳主控制状态机。
 * 汇总任务状态、AprilTag 判定结果和 CH32 执行反馈。
 */

#include "esp_err.h"
#include "app_ch32_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化控制模块内部状态，并为目标 ID 同步做准备。 */
esp_err_t app_ctrl_init(void);

/* 创建并启动控制循环任务。 */
esp_err_t app_ctrl_start(void);

/* 接收 CH32 串口解析结果，通常直接注册给 app_ch32_link_init()。 */
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx);

#ifdef __cplusplus
}
#endif
