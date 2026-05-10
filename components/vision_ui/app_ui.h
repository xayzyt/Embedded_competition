#pragma once

/*
 * LVGL UI 门面接口。
 * 所有公开函数在访问控件前都会获取 BSP 的 LVGL 锁。
 */

#include <stdbool.h>

#include "app_dock_types.h"
#include "app_vision.h"

/* 创建常驻 HUD 控件和交互按钮。 */
bool app_ui_create(void);

/* 在慢速初始化前显示启动加载层。 */
bool app_ui_show_loading(const char *text);

/* 更新启动加载层的状态文本。 */
void app_ui_set_loading_text(const char *text);

/* 隐藏并删除启动加载层。 */
void app_ui_hide_loading(void);

/* 更新主状态文本。 */
void app_ui_set_status(const char *text);

/* 更新视觉识别状态文本。 */
void app_ui_set_vision_text(const char *text);

/* 更新接驳调试状态文本。 */
void app_ui_set_dock_text(const char *text);

/* 更新 AI 抓图状态文本。 */
void app_ui_set_capture_text(const char *text);

/* 根据视觉和接驳判定结果刷新跟踪框、准星、锁定条和认证横幅。 */
void app_ui_update_hud(const app_vision_result_t *vision,
                       const app_dock_judge_result_t *dock);
