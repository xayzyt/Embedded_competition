/*
 * 云端 MQTT 命令 JSON 解析模块内部接口。
 * 将 MQTT 消息 payload 解析为结构化命令。
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 云端下发命令结构。 */
typedef struct {
    char cmd[24];            /* 命令名，如 start_task、set_target、cancel。 */
    uint16_t target_id;      /* 目标 tag ID。 */
    char request_id[32];     /* 云端请求 ID，用于 ACK 回应匹配。 */
    char order_id[48];       /* 真实订单 ID，用于状态回传精确关联。 */
    char order_name[32];     /* 面向 UI 展示的短订单名。 */
} app_cloud_cmd_t;

/* 从 JSON payload 解析命令字段到 app_cloud_cmd_t。 */
esp_err_t app_cloud_cmd_parse_json(const char *payload, app_cloud_cmd_t *out);

#ifdef __cplusplus
}
#endif
