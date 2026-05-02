#pragma once

#include <stdbool.h>

#include "app_vision.h"
#include "app_dock_judge.h"

bool app_ui_create(void);
void app_ui_set_status(const char *text);
void app_ui_set_vision_text(const char *text);
void app_ui_set_dock_text(const char *text);
void app_ui_set_capture_text(const char *text);

/* HUD 叠加层：跟踪框 / 中心准星 / 锁定进度条 / AUTH PASSED */
void app_ui_update_hud(const app_vision_result_t *vision,
                       const app_dock_judge_result_t *dock);
