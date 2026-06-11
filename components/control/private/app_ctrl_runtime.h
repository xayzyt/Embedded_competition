#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_ch32_link.h"
#include "app_dock_judge.h"
#include "app_task.h"
#include "app_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ch32_ready;
    bool dock_busy;
    bool has_weight;
    bool retrigger_blocked;
    bool weather_blocked;
    bool notice_active;
    int32_t weight_g;
    app_ch32_proto_stage_t proto_stage;
    uint8_t proto_error;
    char notice[96];
} app_ctrl_runtime_view_t;

typedef struct {
    uint32_t now_ms;
    app_vision_result_t vision;
    app_dock_judge_result_t dock;
    app_task_snapshot_t task;
    app_ctrl_runtime_view_t runtime;
    bool apriltag_enabled;
    bool ready_level;
    bool prev_ready_level;
    bool prev_dock_busy;
} app_ctrl_cycle_t;

esp_err_t app_ctrl_runtime_init(void);
bool app_ctrl_runtime_is_initialized(void);
void app_ctrl_runtime_on_ch32_line(const app_ch32_line_t *msg);

// Advance mechanical/task state and populate the UI-facing runtime view.
void app_ctrl_runtime_step(app_ctrl_cycle_t *cycle);

#ifdef __cplusplus
}
#endif
