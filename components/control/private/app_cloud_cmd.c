/*
 * app_cloud_cmd.c - 云端 MQTT 命令 JSON 解析实现。
 * 使用 cJSON 从 MQTT payload 中提取 cmd、target_id、request_id 字段。
 */
#include "app_cloud_cmd.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"

/* 从 cJSON 对象中安全读取字符串字段。 */
static bool app_cloud_cmd_get_string(const cJSON *root,
    const char *key,
    char *out,
    size_t out_size)
{
    if (root == NULL || key == NULL || out == NULL || out_size == 0U)
    {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    const bool ok = cJSON_IsString(item) && item->valuestring != NULL;
    if (ok)
    {
        strlcpy(out, item->valuestring, out_size);
    }
    return ok;
}

/* 从 cJSON 对象中安全读取 uint16_t 字段。 */
static bool app_cloud_cmd_get_u16(const cJSON *root, const char *key, uint16_t *out)
{
    if (root == NULL || key == NULL || out == NULL)
    {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    const bool ok = cJSON_IsNumber(item) &&
        item->valuedouble >= 0 &&
        item->valuedouble <= UINT16_MAX;
    if (ok)
    {
        *out = (uint16_t)item->valuedouble;
    }
    return ok;
}

/* 解析云端 JSON 命令，提取 cmd（必选）、target_id 和 request_id（可选）。 */
esp_err_t app_cloud_cmd_parse_json(const char *payload, app_cloud_cmd_t *out)
{
    if (payload == NULL || out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const bool has_cmd = app_cloud_cmd_get_string(root, "cmd", out->cmd, sizeof(out->cmd));
    if (!has_cmd || out->cmd[0] == '\0')
    {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    (void)app_cloud_cmd_get_u16(root, "target_id", &out->target_id);
    (void)app_cloud_cmd_get_string(root, "request_id", out->request_id, sizeof(out->request_id));

    cJSON_Delete(root);
    return ESP_OK;
}
