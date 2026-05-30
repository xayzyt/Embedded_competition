#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// 初始化摄像头、显示缓存和帧处理回调。
esp_err_t app_camera_init(void);
// 开启视频流并启动预览显示任务。
esp_err_t app_camera_preview_start(void);
// 等待第一帧进入 LVGL，便于启动页判断预览是否可见。
bool app_camera_wait_first_frame(uint32_t timeout_ms);
// 读取已经交给 LVGL canvas 的预览帧数。
uint32_t app_camera_display_count(void);
// 等待预览帧数超过 previous，用于切屏前确认有新画面可见。
bool app_camera_wait_display_count_after(uint32_t previous, uint32_t timeout_ms);
// 暂停/恢复帧处理，任务结束或回主屏时用于释放算力。
void app_camera_pause(void);
void app_camera_resume(void);
#ifdef __cplusplus
}
#endif
