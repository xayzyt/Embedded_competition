#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
// AI 样本抓图接口：从相机路由接收抽样帧，后台保存为 BMP 训练样本。

#ifdef __cplusplus
extern "C" {
#endif
// 抓图保存模式，用于采集有/无无人机样本。
typedef enum {
    APP_AI_CAPTURE_MODE_DRONE = 0,
    APP_AI_CAPTURE_MODE_NO_DRONE,
    APP_AI_CAPTURE_MODE_COUNT,
} app_ai_capture_mode_t;
// 初始化抓图存储和写入任务。
esp_err_t app_ai_capture_init(void);
// 开始/停止抓图。
esp_err_t app_ai_capture_start(void);
void app_ai_capture_stop(void);
// 设置、查询和切换抓图模式。
esp_err_t app_ai_capture_set_mode(app_ai_capture_mode_t mode);
app_ai_capture_mode_t app_ai_capture_get_mode(void);
app_ai_capture_mode_t app_ai_capture_toggle_mode(void);
const char *app_ai_capture_mode_label(app_ai_capture_mode_t mode);
// 查询是否需要从相机路由中抓取当前帧。
bool app_ai_capture_is_active(void);
bool app_ai_capture_should_capture_frame(void);
// 提交一帧 RGB565 图像，后台会下采样并写入 BMP。
esp_err_t app_ai_capture_submit_frame(const uint8_t *rgb565,
                                      uint32_t width,
                                      uint32_t height,
                                      size_t len);
#ifdef __cplusplus
}
#endif
