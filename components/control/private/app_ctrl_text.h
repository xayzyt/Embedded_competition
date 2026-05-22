/*
 * 控制模块 UI 文本拼装内部接口。
 * 将接驳判定、任务状态、CH32 协议状态等组装为 UI 可显示的文本。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_ch32_link.h"
#include "app_dock_judge.h"
#include "app_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 拼接接驳调试详情文本，包含偏差、距离、称重等信息。 */
void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
    bool has_weight,
    int32_t weight_g,
    app_ch32_proto_stage_t proto_stage,
    char *buf,
    size_t buf_len);

/* 拼接任务状态文本，包含任务阶段、引导提示和 AI/CH32 状态。 */
void app_ctrl_compose_task_status(const app_task_snapshot_t *task,
    const app_dock_judge_result_t *dock,
    bool ch32_ready,
    bool apriltag_enabled,
    char *buf,
    size_t buf_len);

#ifdef __cplusplus
}
#endif
