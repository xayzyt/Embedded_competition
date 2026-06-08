#pragma once

#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif
