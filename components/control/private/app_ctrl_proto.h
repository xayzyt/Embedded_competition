#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_ch32_link.h"

#ifdef __cplusplus
extern "C" {
#endif

bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage);
bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage);
bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags);
bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error);
const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage);
bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage);
bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
    uint16_t prev_flags,
    app_ch32_proto_stage_t stage,
    uint16_t flags,
    uint8_t proto_error,
    bool cargo_wait_window_seen);

#ifdef __cplusplus
}
#endif
