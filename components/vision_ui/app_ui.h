#pragma once
#include <stdbool.h>
#include "app_dock_types.h"
#include "app_vision.h"
#define APP_UI_FLOW_STEP_COUNT 4
typedef enum {
    APP_UI_FLOW_STEP_DRONE = 0,
    APP_UI_FLOW_STEP_TAG,
    APP_UI_FLOW_STEP_EXEC,
    APP_UI_FLOW_STEP_DONE,
} app_ui_flow_step_t;
typedef enum {
    APP_UI_FLOW_STATE_WAITING = 0,
    APP_UI_FLOW_STATE_ACTIVE,
    APP_UI_FLOW_STATE_DONE,
    APP_UI_FLOW_STATE_ERROR,
} app_ui_flow_state_t;
typedef struct {
    app_ui_flow_step_t active_step;
    app_ui_flow_state_t step_state[APP_UI_FLOW_STEP_COUNT];
    char headline[48];
    char step_detail[APP_UI_FLOW_STEP_COUNT][48];
} app_ui_flow_snapshot_t;
// 创建全局 UI 资源和 HUD 层。
bool app_ui_create(void);
// 启动页显示、进度和隐藏。
bool app_ui_show_loading(const char *text);
void app_ui_set_loading_text(const char *text);
void app_ui_set_loading_progress(int32_t percent);
void app_ui_hide_loading(void);
// 顶部/调试文本更新。
void app_ui_set_status(const char *text);
void app_ui_set_vision_text(const char *text);
void app_ui_set_dock_text(const char *text);
void app_ui_set_capture_text(const char *text);
// 叠加视觉框、锁定条和认证提示。
void app_ui_update_hud(const app_vision_result_t *vision,
                       const app_dock_judge_result_t *dock);
void app_ui_update_control_state(const char *status,
                                 const char *vision_text,
                                 const char *dock_text,
                                 const app_vision_result_t *vision,
                                 const app_dock_judge_result_t *dock,
                                 const app_ui_flow_snapshot_t *flow);
// 主屏显示/隐藏和状态灯更新。
bool app_ui_show_main_screen(void);
void app_ui_hide_main_screen(void);
void app_ui_main_screen_show_pickup(bool show);
void app_ui_main_screen_update_status(bool wifi_ok, bool mqtt_ok, bool ch32_ok);
// 主屏任务状态，用于控制标题、阶段灯和取货提示。
typedef enum {
    APP_UI_MAIN_TASK_WAITING = 0,
    APP_UI_MAIN_TASK_ACTIVE,
    APP_UI_MAIN_TASK_CONFIGURED,
    APP_UI_MAIN_TASK_LOCAL_WAIT,
    APP_UI_MAIN_TASK_CAMERA_FAILED,
    APP_UI_MAIN_TASK_WEATHER_BLOCKED,
    APP_UI_MAIN_TASK_PICKUP_FAILED,
    APP_UI_MAIN_TASK_COMPLETED,
} app_ui_main_task_state_t;
// 主屏任务与天气展示。
void app_ui_main_screen_set_task_state(app_ui_main_task_state_t state);
void app_ui_main_screen_set_task_text(const char *text);
void app_ui_main_screen_set_weather(const char *text, int weather_code);
void app_ui_main_screen_set_weather_simulated(bool simulated);
void app_ui_main_screen_apply_weather_state(const char *weather_text,
                                            int weather_code,
                                            bool simulated,
                                            const char *task_text);
// 主屏按钮回调。
typedef void (*app_ui_pickup_cb_t)(void);
void app_ui_set_pickup_callback(app_ui_pickup_cb_t cb);
typedef void (*app_ui_weather_sim_cb_t)(void);
void app_ui_set_weather_sim_callback(app_ui_weather_sim_cb_t cb);
typedef void (*app_ui_weather_emergency_cb_t)(void);
void app_ui_set_weather_emergency_callback(app_ui_weather_emergency_cb_t cb);
