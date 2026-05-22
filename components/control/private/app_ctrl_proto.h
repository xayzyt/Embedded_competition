/*
 * 控制模块 CH32 协议辅助判断函数内部接口。
 * 提供阶段分类、状态文本、软错误判断等纯函数，供 app_ctrl.c 使用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_ch32_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 判断 CH32 协议阶段是否属于忙碌状态。 */
bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage);
/* 判断当前阶段是否处于等待放货窗口（托盘伸出或等待货物）。 */
bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage);
/* 检查 flags 中托盘已伸出的标志位。 */
bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags);
/* 判断错误码是否属于等待放货期间的软错误（超时/重量异常）。 */
bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error);
/* 将 CH32 协议阶段转为 UI 状态文本。 */
const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage);
/* 判断当前阶段是否需要 busy deadline 超时监控。 */
bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage);
/* 综合判断当前是否应视为等待放货软错误而非硬故障。 */
bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
    uint16_t prev_flags,
    app_ch32_proto_stage_t stage,
    uint16_t flags,
    uint8_t proto_error,
    bool cargo_wait_window_seen);

#ifdef __cplusplus
}
#endif
