#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_ch32_link.h"
// CH32 协议语义辅助：控制器用这些小函数避免直接散落阶段/flags 判断。

#ifdef __cplusplus
extern "C" {
#endif
// 判断 CH32 阶段是否表示对接机构正在动作或等待货物。
bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage);
bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage);
// 从协议 flags 中解析托盘是否伸出。
bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags);
// 判断协议错误是否可视为“等待货物”的软错误。
bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error);
// 将 CH32 阶段转换成 UI 状态文案。
const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage);
// 非货物等待阶段需要 busy 超时保护。
bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage);
// 综合前后状态判断是否应把故障解释为等待货物超时/重量异常。
bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
    uint16_t prev_flags,
    app_ch32_proto_stage_t stage,
    uint16_t flags,
    uint8_t proto_error,
    bool cargo_wait_window_seen);
#ifdef __cplusplus
}
#endif
