#pragma once

#include "app_ctrl_runtime.h"

// 将控制循环结果整理为 UI 文案和流程状态。
#ifdef __cplusplus
extern "C" {
#endif

// 更新状态栏、调试信息、识别框和流程条。
void app_ctrl_ui_publish(const app_ctrl_cycle_t *cycle);

#ifdef __cplusplus
}
#endif
