#pragma once

#include <stdbool.h>
#include <stdint.h>

bool app_display_init(void);
void app_display_lock(void);
void app_display_unlock(void);
bool app_display_touch_read(int32_t *x, int32_t *y);
