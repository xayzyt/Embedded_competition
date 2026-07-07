#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_AUDIO_PROMPT_READY = 0,
    APP_AUDIO_PROMPT_DOCKING_COMPLETE,
    APP_AUDIO_PROMPT_OUTER_DOOR_OPENING,
    APP_AUDIO_PROMPT_WEATHER_PAUSED,
    APP_AUDIO_PROMPT_TRAY_RETRACTING,
    APP_AUDIO_PROMPT_APRILTAG_LOCATED,
    APP_AUDIO_PROMPT_DRONE_IDENTIFIED,
    APP_AUDIO_PROMPT_DRONE_NOT_DETECTED,
    APP_AUDIO_PROMPT_DRONE_RETURNED,
    APP_AUDIO_PROMPT_COUNT,
} app_audio_prompt_id_t;

esp_err_t app_audio_prompt_init(void);
bool app_audio_prompt_is_enabled(void);
esp_err_t app_audio_prompt_set_enabled(bool enabled, bool persist);
esp_err_t app_audio_prompt_request(app_audio_prompt_id_t prompt);
esp_err_t app_audio_prompt_request_ready(void);
esp_err_t app_audio_prompt_request_docking_complete(void);
esp_err_t app_audio_prompt_request_outer_door_opening(void);
esp_err_t app_audio_prompt_request_weather_paused(void);
esp_err_t app_audio_prompt_request_tray_retracting(void);
esp_err_t app_audio_prompt_request_apriltag_located(void);
esp_err_t app_audio_prompt_request_drone_identified(void);
esp_err_t app_audio_prompt_request_drone_not_detected(void);
esp_err_t app_audio_prompt_request_drone_returned(void);

#ifdef __cplusplus
}
#endif
