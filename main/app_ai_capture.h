#pragma once

/*
 * 手动 AI 数据集抓拍模块公开接口。
 * 摄像头任务负责提交帧，UI 负责切换抓拍类别。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_AI_CAPTURE_MODE_DRONE = 0,
    APP_AI_CAPTURE_MODE_NO_DRONE,
    APP_AI_CAPTURE_MODE_COUNT,
} app_ai_capture_mode_t;

/* 分配队列和缓存，并准备 SD 卡抓拍目录。 */
esp_err_t app_ai_capture_init(void);

/* 开始连续抓拍当前类别的数据集图片。 */
esp_err_t app_ai_capture_start(void);

/* 停止连续抓拍。 */
void app_ai_capture_stop(void);

/* 设置当前抓拍类别。 */
esp_err_t app_ai_capture_set_mode(app_ai_capture_mode_t mode);

/* 读取当前抓拍类别。 */
app_ai_capture_mode_t app_ai_capture_get_mode(void);

/* 在有无人机/无无人机两个类别之间切换。 */
app_ai_capture_mode_t app_ai_capture_toggle_mode(void);

/* 返回抓拍类别在 UI 和文件夹中使用的短标签。 */
const char *app_ai_capture_mode_label(app_ai_capture_mode_t mode);

/* 判断抓拍流程当前是否处于启用状态。 */
bool app_ai_capture_is_active(void);

/* 判断当前帧是否应该送入抓拍队列。 */
bool app_ai_capture_should_capture_frame(void);

/* 提交一帧 RGB565 图像，模块内部会下采样并异步写盘。 */
esp_err_t app_ai_capture_submit_frame(const uint8_t *rgb565,
                                      uint32_t width,
                                      uint32_t height,
                                      size_t len);

#ifdef __cplusplus
}
#endif
