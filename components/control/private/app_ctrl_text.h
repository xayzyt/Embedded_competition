#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_ch32_link.h"
#include "app_dock_judge.h"
#include "app_task.h"
// 控制状态文案生成：把业务判断和字符串格式化隔离，控制主循环只传快照。

#ifdef __cplusplus
extern "C" {
#endif
// 生成调试详情行，包含视觉偏差、距离、姿态、阶段和重量。
void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
    bool has_weight,
    int32_t weight_g,
    app_ch32_proto_stage_t proto_stage,
    char *buf,
    size_t buf_len);
// 生成主状态行，根据任务、视觉门控和 CH32 在线状态给 UI 使用。
void app_ctrl_compose_task_status(const app_task_snapshot_t *task,
    const app_dock_judge_result_t *dock,
    bool ch32_ready,
    bool apriltag_enabled,
    char *buf,
    size_t buf_len);
#ifdef __cplusplus
}
#endif
