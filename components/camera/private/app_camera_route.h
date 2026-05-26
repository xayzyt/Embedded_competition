#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ai_due;
    bool vision_due;
    bool capture_due;
} app_camera_frame_route_t;

void app_camera_route_reset(void);
app_camera_frame_route_t app_camera_route_select(void);
void app_camera_route_note_ai_submit(void);
void app_camera_route_note_vision_submit(void);
void app_camera_route_note_capture_submit(void);
void app_camera_route_maybe_log_diag(uint32_t frame_count,
    uint32_t display_count,
    uint32_t stage_drop_count);

#ifdef __cplusplus
}
#endif
