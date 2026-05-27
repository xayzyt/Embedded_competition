#include "app_cloud_cmd.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "cJSON.h"

// 云端命令 JSON 解析：只抽取业务需要的字段，缺省字段保持为 0/空串。

// 读取字符串字段并按目标缓冲区截断，避免 MQTT 负载过长越界。
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

// 读取 0~65535 范围内的数值字段，用作目标标签 ID。
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

// 解析云端命令，cmd 为必填，其余字段按不同命令可选。
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
    (void)app_cloud_cmd_get_string(root, "order_id", out->order_id, sizeof(out->order_id));
    (void)app_cloud_cmd_get_string(root, "order_name", out->order_name, sizeof(out->order_name));
    cJSON_Delete(root);
    return ESP_OK;
}
