#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "app_ch32_link.h"
#include "app_dock_judge.h"
#include "app_task.h"
#include "app_vision.h"

// 控制任务内部数据结构：保存单次循环的输入、判定结果和机械状态。
#ifdef __cplusplus
extern "C" {
#endif

// 获取当前毫秒时间戳（基于 FreeRTOS tick），控制模块共用。
static inline uint32_t app_ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// 供 UI 显示的 CH32 运行状态。
typedef struct {
    bool ch32_ready;                    // CH32 是否在线并处于可用状态。
    bool dock_busy;                     // 接驳机构是否正在动作或等待货物。
    bool has_weight;                    // 是否收到有效重量。
    bool retrigger_blocked;             // 是否处于重复触发冷却时间。
    bool weather_blocked;               // 本轮接驳是否被天气阻止。
    bool notice_active;                 // 临时提示是否仍需显示。
    int32_t weight_g;                   // 最近重量，单位 g。
    app_ch32_proto_stage_t proto_stage; // 最近机械阶段。
    uint8_t proto_error;                // 最近错误码。
    char notice[96];                    // 临时状态提示。
} app_ctrl_runtime_view_t;

// 控制任务单次循环使用的数据。
typedef struct {
    uint32_t now_ms;                 // 本轮开始时间。
    app_vision_result_t vision;      // 最新视觉结果。
    app_dock_judge_result_t dock;    // 对接判定结果。
    app_task_snapshot_t task;        // 当前任务状态。
    app_ctrl_runtime_view_t runtime; // CH32 状态和提示。
    bool apriltag_enabled;           // 是否允许使用 AprilTag 结果。
    bool ready_level;                // 是否满足自动接驳条件。
    bool prev_ready_level;           // 上一轮 READY 状态。
    bool prev_dock_busy;             // 上一轮机构忙碌状态。
} app_ctrl_cycle_t;

// 初始化内部机械状态，可重复调用。
esp_err_t app_ctrl_runtime_init(void);
bool app_ctrl_runtime_is_initialized(void);
// 接收 CH32 解析后的消息并更新内部状态。
void app_ctrl_runtime_on_ch32_line(const app_ch32_line_t *msg);

// 处理超时、自动接驳和任务状态变化，并生成 UI 所需状态。
void app_ctrl_runtime_step(app_ctrl_cycle_t *cycle);

#ifdef __cplusplus
}
#endif
