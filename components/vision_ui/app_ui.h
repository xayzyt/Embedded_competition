#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "app_dock_types.h"
#include "app_vision.h"

// UI 公共接口：负责启动页、主屏和相机预览 HUD。
// 公共更新函数内部会获取 LVGL 锁，业务任务可以直接调用。

#define APP_UI_FLOW_STEP_COUNT 4

// 任务流程条的固定阶段顺序。
typedef enum {
    APP_UI_FLOW_STEP_DRONE = 0,
    APP_UI_FLOW_STEP_TAG,
    APP_UI_FLOW_STEP_EXEC,
    APP_UI_FLOW_STEP_DONE,
} app_ui_flow_step_t;

// 单个流程阶段在 UI 上的展示状态。
typedef enum {
    APP_UI_FLOW_STATE_WAITING = 0,
    APP_UI_FLOW_STATE_ACTIVE,
    APP_UI_FLOW_STATE_DONE,
    APP_UI_FLOW_STATE_ERROR,
} app_ui_flow_state_t;

// 控制器推送给 UI 的流程快照，UI 只按快照刷新，不反查控制内部状态。
typedef struct {
    app_ui_flow_step_t active_step;                         // 当前高亮阶段。
    app_ui_flow_state_t step_state[APP_UI_FLOW_STEP_COUNT]; // 每个阶段的状态灯。
    char headline[48];                                      // 流程面板标题。
    char step_detail[APP_UI_FLOW_STEP_COUNT][48];           // 每个阶段的简短说明。
} app_ui_flow_snapshot_t;

#define APP_UI_COCKPIT_GATE_COUNT 5

typedef enum {
    APP_UI_COCKPIT_GATE_WAITING = 0,
    APP_UI_COCKPIT_GATE_ACTIVE,
    APP_UI_COCKPIT_GATE_ADJUST,
    APP_UI_COCKPIT_GATE_PASSED,
    APP_UI_COCKPIT_GATE_BLOCKED,
} app_ui_cockpit_gate_state_t;

// 普通配送预览的只读视图模型。安全接管不使用该结构。
typedef struct {
    uint32_t task_generation;
    uint32_t updated_ms;
    uint16_t target_id;
    bool task_active;
    bool ai_valid;
    bool ai_confirmed;
    float ai_drone_score;
    uint8_t ai_hit_count;
    uint8_t ai_confirm_hits;
    app_vision_result_t vision;
    app_dock_judge_result_t dock;
    bool use_distance_gate;
    int32_t min_distance_mm;
    int32_t max_distance_mm;
    int32_t center_x_tol;
    int32_t center_y_tol;
    uint16_t stable_required;
    bool weather_blocked;
    bool delivery_permitted;
    app_ui_cockpit_gate_state_t gate[APP_UI_COCKPIT_GATE_COUNT];
} app_ui_cockpit_view_t;

typedef enum {
    APP_UI_RECEIPT_PHOTO_PROCESSING = 0,
    APP_UI_RECEIPT_PHOTO_READY,
    APP_UI_RECEIPT_PHOTO_FAILED,
} app_ui_receipt_photo_state_t;

typedef struct {
    uint32_t task_generation;
    bool ai_valid;
    float ai_confidence;
    bool tag_valid;
    uint16_t tag_id;
    int32_t distance_mm;
    uint16_t stable_frames;
    bool weight_valid;
    int32_t weight_g;
    uint32_t total_duration_ms;
    app_ui_receipt_photo_state_t photo_state;
    char photo_sha256_prefix[9];
} app_ui_completion_receipt_view_t;

// 普通任务进入相机预览前的只读任务信息。Tag 0 是合法目标，不能用数值判断有效性。
typedef struct {
    uint32_t generation;
    bool target_valid;
    uint16_t target_id;
} app_ui_task_intro_view_t;

// 创建预览 HUD 和公共 UI 对象。
bool app_ui_create(void);

// 启动页显示、进度和隐藏。
bool app_ui_show_loading(void);
void app_ui_set_loading_progress(int32_t percent);
void app_ui_hide_loading(void);

// 订单进入预览前的短暂任务接收页。
bool app_ui_show_task_intro(const app_ui_task_intro_view_t *view);
void app_ui_hide_task_intro(void);

// 顶部状态和控制器统一刷新入口。
void app_ui_set_status(const char *text);

// 控制器统一刷新状态栏、任务行、调试行和 HUD。
void app_ui_update_control_state(const char *status,
                                 const char *vision_text,
                                 const char *dock_text,
                                 const app_vision_result_t *vision,
                                 const app_dock_judge_result_t *dock,
                                 const app_ui_flow_snapshot_t *flow);

// 普通模式精简座舱；普通预览隐藏时调用会直接返回。
void app_ui_update_cockpit(const app_ui_cockpit_view_t *view);

// 普通任务完成凭证，安全接管任务不得调用。
bool app_ui_show_completion_receipt(const app_ui_completion_receipt_view_t *view);
void app_ui_update_completion_receipt(const app_ui_completion_receipt_view_t *view);
bool app_ui_completion_receipt_is_visible(void);

// 主屏显示/隐藏和状态灯更新。
bool app_ui_show_main_screen(void);
void app_ui_hide_main_screen(void);
void app_ui_set_preview_hud_visible(bool visible);
void app_ui_main_screen_update_status(bool wifi_ok, bool mqtt_ok, bool ch32_ok);

// 主屏任务状态，用于控制标题、阶段灯和提示。
typedef enum {
    APP_UI_MAIN_TASK_WAITING = 0,
    APP_UI_MAIN_TASK_ACTIVE,
    APP_UI_MAIN_TASK_CONFIGURED,
    APP_UI_MAIN_TASK_LOCAL_WAIT,
    APP_UI_MAIN_TASK_CAMERA_FAILED,
    APP_UI_MAIN_TASK_WEATHER_BLOCKED,
    APP_UI_MAIN_TASK_COMPLETED,
} app_ui_main_task_state_t;

// 主屏任务与天气展示。
void app_ui_main_screen_set_task_state(app_ui_main_task_state_t state);
void app_ui_main_screen_set_task_text(const char *text);
void app_ui_main_screen_set_weather(const char *text, int weather_code);
void app_ui_main_screen_set_weather_simulated(bool simulated);
void app_ui_main_screen_set_voice_enabled(bool enabled);
void app_ui_main_screen_apply_weather_state(const char *weather_text,
                                            int weather_code,
                                            bool simulated,
                                            const char *task_text);

// 异常演示界面：不启动摄像头，只展示 CH32 机械状态和天气保护入口。
typedef enum {
    APP_UI_EXCEPTION_DEMO_READY = 0,
    APP_UI_EXCEPTION_DEMO_STARTING,
    APP_UI_EXCEPTION_DEMO_RUNNING,
    APP_UI_EXCEPTION_DEMO_WEATHER,
    APP_UI_EXCEPTION_DEMO_SAFE,
    APP_UI_EXCEPTION_DEMO_FAILED,
    APP_UI_EXCEPTION_DEMO_CH32_OFFLINE,
} app_ui_exception_demo_state_t;

bool app_ui_show_exception_demo_screen(void);
void app_ui_exception_demo_set_state(app_ui_exception_demo_state_t state);
void app_ui_exception_demo_update_ch32(int stage, uint8_t error);

typedef void (*app_ui_exception_demo_cb_t)(void);
void app_ui_set_exception_demo_callback(app_ui_exception_demo_cb_t cb);

typedef void (*app_ui_voice_toggle_cb_t)(void);
void app_ui_set_voice_toggle_callback(app_ui_voice_toggle_cb_t cb);

typedef void (*app_ui_exception_back_cb_t)(void);
void app_ui_set_exception_back_callback(app_ui_exception_back_cb_t cb);

// 异常演示页天气模拟回调。
typedef void (*app_ui_weather_sim_cb_t)(void);
void app_ui_set_weather_sim_callback(app_ui_weather_sim_cb_t cb);

// 安全接管预览 HUD：叠加在摄像头画面上，用于现场展示离场预警和台风接管。
typedef enum {
    APP_UI_SAFETY_TAKEOVER_IDLE = 0,
    APP_UI_SAFETY_TAKEOVER_STARTING,
    APP_UI_SAFETY_TAKEOVER_WAIT_DRONE,
    APP_UI_SAFETY_TAKEOVER_DRONE_CONFIRMED,
    APP_UI_SAFETY_TAKEOVER_AUTH_PASSED,
    APP_UI_SAFETY_TAKEOVER_WINDOW_OPEN,
    APP_UI_SAFETY_TAKEOVER_DRONE_LOST,
    APP_UI_SAFETY_TAKEOVER_DRONE_RECOVERED,
    APP_UI_SAFETY_TAKEOVER_TYPHOON,
    APP_UI_SAFETY_TAKEOVER_SAFE_DONE,
    APP_UI_SAFETY_TAKEOVER_FAILED,
} app_ui_safety_takeover_state_t;

typedef struct {
    app_ui_safety_takeover_state_t state;
    int32_t countdown_s;
    uint16_t target_id;
    const char *phase_text;
    const char *status_title;
    const char *status_detail;
} app_ui_safety_takeover_view_t;

typedef void (*app_ui_safety_typhoon_cb_t)(void);
void app_ui_set_safety_typhoon_callback(app_ui_safety_typhoon_cb_t cb);
void app_ui_safety_takeover_set_visible(bool visible);
bool app_ui_safety_takeover_set_view(const app_ui_safety_takeover_view_t *view);
void app_ui_safety_takeover_set_state(app_ui_safety_takeover_state_t state,
                                      int32_t countdown_s,
                                      uint16_t target_id);
