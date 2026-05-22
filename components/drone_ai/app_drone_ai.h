/*
 * 无人机 AI 检测模块公开接口。
 * 用于在 AprilTag 门控开启前，先通过端侧 AI 模型确认画面中是否存在无人机。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AI 分类结果标签。 */
typedef enum {
    APP_DRONE_AI_CLASS_NODRONE = 0,  /* 未检测到无人机。 */
    APP_DRONE_AI_CLASS_DRONE = 1,    /* 检测到无人机。 */
} app_drone_ai_class_t;

/* 单帧 AI 推理输出结果。 */
typedef struct {
    bool valid;                     /* 当前结果是否有效。 */
    bool confirmed;                 /* 是否连续多帧确认检测到无人机。 */
    app_drone_ai_class_t label;     /* 分类标签。 */
    float nodrone_score;            /* "无无人机"分类置信度。 */
    float drone_score;              /* "有无人机"分类置信度。 */
    uint8_t hit_count;              /* 连续检测到无人机的帧数。 */
    uint32_t frame_seq;             /* 推理帧序号。 */
    uint32_t infer_ms;              /* 推理耗时（毫秒）。 */
} app_drone_ai_result_t;

/* AI 推理统计计数器。 */
typedef struct {
    uint32_t submitted;              /* 已提交帧数。 */
    uint32_t inferred;               /* 已完成推理帧数。 */
    uint32_t dropped;                /* 因繁忙丢弃的帧数。 */
    bool confirmed;                  /* 是否已有确认结果。 */
} app_drone_ai_stats_t;

/* 初始化 AI 模型和推理引擎。 */
esp_err_t app_drone_ai_init(void);

/* 提交一帧 RGB565 图像供后台推理。 */
esp_err_t app_drone_ai_submit_frame(const uint8_t *rgb565,
                                    uint32_t width,
                                    uint32_t height,
                                    size_t len);

/* 读取最近一次推理结果。 */
bool app_drone_ai_get_latest_result(app_drone_ai_result_t *out);

/* 查询 AI 模型是否加载就绪。 */
bool app_drone_ai_is_model_ready(void);

/* 等待 AI 模型就绪，可指定超时。 */
esp_err_t app_drone_ai_wait_ready(uint32_t timeout_ms);

/* 查询是否已连续多帧确认检测到无人机。 */
bool app_drone_ai_is_drone_confirmed(void);

/* 重置门控状态，新任务开始时调用。 */
void app_drone_ai_reset_gate(void);

/* 格式化一行 AI 状态文本供 UI 显示。 */
void app_drone_ai_format_status(char *buf, size_t buf_len);

/* 读取 AI 推理统计计数器。 */
void app_drone_ai_get_stats(app_drone_ai_stats_t *out);

#ifdef __cplusplus
}
#endif
