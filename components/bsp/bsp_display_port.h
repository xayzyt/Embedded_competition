#pragma once
#include <stdbool.h>

// 初始化 BSP 显示和 LVGL 端口。
bool app_display_init(void);
// 打开背光，通常在 UI 首屏创建完成后调用。
void app_display_backlight_on(void);
