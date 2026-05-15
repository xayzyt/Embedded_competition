#include "app_ctrl_proto.h"

bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage)
{
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
    return (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
    (stage == APP_CH32_STAGE_WAITING_CARGO);
}

bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags)
{
    return (flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U;
}

bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error)
{
    return (proto_error == APP_CH32_ERR_TIMEOUT) ||
    (proto_error == APP_CH32_ERR_WEIGHT);
}

const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage)
{
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
