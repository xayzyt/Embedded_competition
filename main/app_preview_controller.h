#pragma once

#include "esp_err.h"

// 任务状态驱动的相机预览切换接口。
#ifdef __cplusplus
extern "C" {
#endif

// 创建事件处理任务并注册任务状态回调，可重复调用。
esp_err_t app_preview_controller_start(void);

#ifdef __cplusplus
}
#endif
