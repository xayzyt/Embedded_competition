#ifndef APP_CLOUD_H
#define APP_CLOUD_H

#include "esp_err.h"
#include "app_task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_cloud_init(void);
void app_cloud_publish_task_snapshot(const app_task_snapshot_t *snap);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOUD_H */
