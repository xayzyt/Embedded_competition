#pragma once

/*
 * 摄像头预览流水线入口。
 * 本模块负责 V4L2 采集、预览画布刷新和视觉抽样。
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配摄像头/显示缓存，创建预览画布并注册帧回调。 */
esp_err_t app_camera_init(void);

/* 打开摄像头视频流，开始向 LVGL 和 app_vision 推送帧。 */
esp_err_t app_camera_preview_start(void);

/* Wait until the first preview frame is actually bound to the LVGL canvas. */
bool app_camera_wait_first_frame(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
