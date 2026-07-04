#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
// 无人机 AI 接口：异步处理相机帧，并输出连续识别确认结果。

#ifdef __cplusplus
extern "C" {
#endif
// 无人机 AI 模块吞吐和确认状态统计。
typedef struct {
    uint32_t submitted;  // 已提交帧数。
    uint32_t inferred;   // 已完成推理帧数。
    uint32_t dropped;    // 忙碌时丢弃帧数。
    uint8_t hit_count;   // 当前任务累计的有效命中帧数。
    uint8_t confirm_hits; // 需要达到的确认帧数。
    bool confirmed;      // 是否已确认无人机存在。
} app_drone_ai_stats_t;
// 创建推理缓存、队列和 AI 任务。
esp_err_t app_drone_ai_init(void);
// 提交 RGB565 帧；函数返回前已复制数据，调用者可以继续复用源缓冲。
// 队列忙或已经确认无人机时直接跳过本帧。
esp_err_t app_drone_ai_submit_frame(const uint8_t *rgb565,
                                    uint32_t width,
                                    uint32_t height,
                                    size_t len);
// 请求加载模型并等待就绪。
esp_err_t app_drone_ai_wait_ready(uint32_t timeout_ms);
// 查询识别状态；新任务开始前调用 reset_gate 清空连续命中。
bool app_drone_ai_is_drone_confirmed(void);
bool app_drone_ai_is_busy(void);
void app_drone_ai_reset_gate(void);
void app_drone_ai_set_continuous(bool enabled);
uint32_t app_drone_ai_last_drone_seen_ms(void);
// 生成调试文案和读取统计。
void app_drone_ai_format_status(char *buf, size_t buf_len);
void app_drone_ai_get_stats(app_drone_ai_stats_t *out);
#ifdef __cplusplus
}
#endif
