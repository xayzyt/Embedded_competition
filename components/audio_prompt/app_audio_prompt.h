#pragma once

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
    APP_AUDIO_PROMPT_COUNT,
} app_audio_prompt_id_t;

esp_err_t app_audio_prompt_init(void);
esp_err_t app_audio_prompt_request(app_audio_prompt_id_t prompt);
esp_err_t app_audio_prompt_request_ready(void);
esp_err_t app_audio_prompt_request_docking_complete(void);
esp_err_t app_audio_prompt_request_outer_door_opening(void);
esp_err_t app_audio_prompt_request_weather_paused(void);
esp_err_t app_audio_prompt_request_tray_retracting(void);
esp_err_t app_audio_prompt_request_apriltag_located(void);
esp_err_t app_audio_prompt_request_drone_identified(void);

#ifdef __cplusplus
}
#endif
