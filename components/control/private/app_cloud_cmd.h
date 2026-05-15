#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char cmd[24];
    uint16_t target_id;
    char request_id[32];
} app_cloud_cmd_t;

esp_err_t app_cloud_cmd_parse_json(const char *payload, app_cloud_cmd_t *out);

#ifdef __cplusplus
}
#endif
