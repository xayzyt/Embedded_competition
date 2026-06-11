#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register task-state handling and own the camera preview lifecycle.
esp_err_t app_preview_controller_start(void);

#ifdef __cplusplus
}
#endif
