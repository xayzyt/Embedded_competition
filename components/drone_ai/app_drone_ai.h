#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// 无人机 AI 模块吞吐和确认状态统计。
typedef struct {
    uint32_t submitted;  // 已提交帧数。
    uint32_t inferred;   // 已完成推理帧数。
    uint32_t dropped;    // 忙碌时丢弃帧数。
    bool confirmed;      // 是否已确认无人机存在。
} app_drone_ai_stats_t;
// 初始化模型运行环境。
esp_err_t app_drone_ai_init(void);
// 提交一帧 RGB565 图像给 AI 推理。
esp_err_t app_drone_ai_submit_frame(const uint8_t *rgb565,
                                    uint32_t width,
                                    uint32_t height,
                                    size_t len);
// 等待模型加载完成。
esp_err_t app_drone_ai_wait_ready(uint32_t timeout_ms);
// 查询/重置 AI 门控。
bool app_drone_ai_is_drone_confirmed(void);
void app_drone_ai_reset_gate(void);
// 格式化状态与读取统计。
void app_drone_ai_format_status(char *buf, size_t buf_len);
void app_drone_ai_get_stats(app_drone_ai_stats_t *out);
#ifdef __cplusplus
}
#endif
