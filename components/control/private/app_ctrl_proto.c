#include "app_ctrl_proto.h"
// CH32 协议辅助判断：把底层阶段、flags 和错误码转换成控制语义。

bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage)
{
    // 这些阶段表示机构正在运动或等待货物，控制层需要保持 busy。
    switch (stage) {
    case APP_CH32_STAGE_DOOR_OPENING:
    case APP_CH32_STAGE_DOOR_OPENED:
    case APP_CH32_STAGE_TRAY_EXTENDING:
    case APP_CH32_STAGE_TRAY_EXTENDED:
    case APP_CH32_STAGE_WAITING_CARGO:
    case APP_CH32_STAGE_CARGO_DETECTED:
    case APP_CH32_STAGE_TRAY_RETRACTING:
    case APP_CH32_STAGE_TRAY_RETRACTED:
    case APP_CH32_STAGE_DOOR_CLOSING:
        return true;
    default:
        return false;
    }
}
bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage)
{
    // 托盘已伸出或正在等待货物时，超时/重量异常按软等待处理。
    return (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
    (stage == APP_CH32_STAGE_WAITING_CARGO);
}
bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags)
{
    // flags 是 CH32 的实时限位输入，托盘伸出可独立于阶段机确认。
    return (flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U;
}
bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error)
{
    // 等货窗口内的超时/重量异常代表“继续等货”，不是硬故障。
    return (proto_error == APP_CH32_ERR_TIMEOUT) ||
    (proto_error == APP_CH32_ERR_WEIGHT);
}
const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage)
{
    // 控制器和 UI 复用同一套短文案，保持现场日志与屏幕一致。
    switch (stage) {
    case APP_CH32_STAGE_IDLE:            return "dock: CH32 idle";
    case APP_CH32_STAGE_READY:           return "dock: CH32 ready";
    case APP_CH32_STAGE_DOOR_OPENING:    return "dock: door opening";
    case APP_CH32_STAGE_DOOR_OPENED:     return "dock: door opened";
    case APP_CH32_STAGE_TRAY_EXTENDING:  return "dock: tray extending";
    case APP_CH32_STAGE_TRAY_EXTENDED:   return "dock: tray extended";
    case APP_CH32_STAGE_WAITING_CARGO:   return "dock: waiting cargo";
    case APP_CH32_STAGE_CARGO_DETECTED:  return "dock: cargo detected";
    case APP_CH32_STAGE_TRAY_RETRACTING: return "dock: tray retracting";
    case APP_CH32_STAGE_TRAY_RETRACTED:  return "dock: tray retracted";
    case APP_CH32_STAGE_DOOR_CLOSING:    return "dock: door closing";
    case APP_CH32_STAGE_SAFE_LOCKED:     return "dock: safe locked";
    case APP_CH32_STAGE_COMPLETE:        return "dock: cycle complete";
    case APP_CH32_STAGE_FAULT:           return "dock: CH32 fault";
    case APP_CH32_STAGE_UNKNOWN:
    default:                             return "dock: CH32 online";
    }
}
bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage)
{
    // 等货阶段没有固定动作时限，其余 busy 阶段需要超时保护。
    return !app_ctrl_proto_stage_is_cargo_wait_window(stage);
}
bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
    uint16_t prev_flags,
    app_ch32_proto_stage_t stage,
    uint16_t flags,
    uint8_t proto_error,
    bool cargo_wait_window_seen)
{
    const bool tray_out_now = app_ctrl_proto_flags_indicate_tray_out(flags);
    const bool tray_out_before = app_ctrl_proto_flags_indicate_tray_out(prev_flags);
    // 等货超时或重量异常在托盘伸出窗口内属于可恢复等待，不立即进入故障。
    if (!app_ctrl_proto_error_is_cargo_wait_soft(proto_error))
    {
        return false;
    }
    if ((stage == APP_CH32_STAGE_FAULT) ||
        (stage == APP_CH32_STAGE_SAFE_LOCKED) ||
        (stage == APP_CH32_STAGE_COMPLETE) ||
        ((flags & APP_CH32_FLAG_LOCKED) != 0U))
    {
        return false;
    }
    if (cargo_wait_window_seen)
    {
        return app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        tray_out_now ||
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage) ||
        tray_out_before;
    }
    if (app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage))
    {
        return true;
    }
    return tray_out_now &&
    ((stage == APP_CH32_STAGE_TRAY_EXTENDING) ||
        (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
        (stage == APP_CH32_STAGE_WAITING_CARGO));
}
