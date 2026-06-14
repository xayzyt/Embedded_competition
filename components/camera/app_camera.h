#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
// 摄像头预览接口：负责初始化、启流、首帧等待和暂停恢复。

#ifdef __cplusplus
extern "C" {
#endif
// 初始化摄像头、显示缓存、LVGL canvas 和帧回调。
esp_err_t app_camera_init(void);
// 注册 V4L2 USERPTR 缓冲并启动视频流。
esp_err_t app_camera_preview_start(void);
// 等待第一帧送入 LVGL，超时返回 false。
bool app_camera_wait_first_frame(uint32_t timeout_ms);
// 返回已经显示到 LVGL canvas 的帧数。
uint32_t app_camera_display_count(void);
// 等待显示帧数超过 previous，供切屏前确认画面已经更新。
bool app_camera_wait_display_count_after(uint32_t previous, uint32_t timeout_ms);
// 暂停或恢复帧处理；不会关闭底层视频流。
void app_camera_pause(void);
void app_camera_resume(void);
#ifdef __cplusplus
}
#endif
