#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
// MQTT 命令解析接口：只把 JSON 中的业务字段折叠成固定长度结构体。

#ifdef __cplusplus
extern "C" {
#endif
// 云端下发命令的解析结果。
typedef struct {
    char cmd[24];          // 命令名，例如 set_target/start/cancel。
    uint16_t target_id;    // 目标 AprilTag ID。
    bool voice_enabled_set; // 是否携带语音开关字段。
    bool voice_enabled;    // 语音播报开关。
    char request_id[32];   // 云端请求 ID，用于 ACK 对应。
    char order_id[48];     // 订单 ID。
    char order_name[32];   // 订单显示名。
} app_cloud_cmd_t;
// 从 MQTT JSON 负载中解析云端命令。
esp_err_t app_cloud_cmd_parse_json(const char *payload, app_cloud_cmd_t *out);
#ifdef __cplusplus
}
#endif
