#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
// 无人机 AI 接口：异步处理相机帧，并输出连续识别确认结果。

#ifdef __cplusplus
extern "C" {
#endif
#define APP_DRONE_AI_SAMPLE_INTERVAL_FRAMES 8U

// AI 最近一次推理的分类结果。该快照只读，不参与门控判定。
typedef enum {
    APP_DRONE_AI_CLASS_NODRONE = 0,
    APP_DRONE_AI_CLASS_DRONE = 1,
} app_drone_ai_class_t;

typedef struct {
    bool valid;                       // 是否已有有效推理结果。
    bool confirmed;                   // 当前任务是否已确认无人机。
    app_drone_ai_class_t label;       // 最近一次推理类别。
    float nodrone_score;              // 非无人机 softmax 分数。
    float drone_score;                // 无人机 softmax 分数。
    uint8_t hit_count;                // 当前任务有效命中数。
    uint8_t confirm_hits;             // 确认所需命中数。
    uint32_t motion_score;            // 当前画面运动量。
    uint32_t frame_seq;               // 推理输入帧序号。
    uint32_t infer_ms;                // 最近一次推理耗时。
    uint32_t result_ms;               // 最近结果产生时间。
} app_drone_ai_snapshot_t;

// 无人机 AI 模块吞吐和确认状态统计。
typedef struct {
    uint32_t submitted;  // 已提交帧数。
    uint32_t inferred;   // 已完成推理帧数。
    uint32_t dropped;    // 忙碌时丢弃帧数。
    uint32_t last_drone_seen_ms; // 最近一次稳定识别到无人机的时间。
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
bool app_drone_ai_get_snapshot(app_drone_ai_snapshot_t *out);
#ifdef __cplusplus
}
#endif
