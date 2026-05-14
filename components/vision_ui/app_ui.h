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

/* 更新启动加载层进度条（0-100）。 */
void app_ui_set_loading_progress(int32_t percent);

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

/* Update control-loop text and HUD in one LVGL lock. */
void app_ui_update_control_state(const char *status,
                                 const char *vision_text,
                                 const char *dock_text,
                                 const app_vision_result_t *vision,
                                 const app_dock_judge_result_t *dock);

/* -------------------------------------------------------------------------- */
/* 主界面（仪表盘）                                                           */
/* -------------------------------------------------------------------------- */

/* 创建并显示主屏幕。 */
bool app_ui_show_main_screen(void);

/* 隐藏主屏幕（切换到摄像头 HUD）。 */
void app_ui_hide_main_screen(void);

/* 在主屏幕上显示或隐藏"取货"按钮。 */
void app_ui_main_screen_show_pickup(bool show);

/* 更新主屏幕状态指示器。 */
void app_ui_main_screen_update_status(bool wifi_ok, bool mqtt_ok, bool ch32_ok);

/* 设置主屏幕任务状态文本。 */
void app_ui_main_screen_set_task_text(const char *text);

/* 注册"取货"按钮回调（由 main 设置，避免 UI 层依赖 control）。 */
typedef void (*app_ui_pickup_cb_t)(void);
void app_ui_set_pickup_callback(app_ui_pickup_cb_t cb);
