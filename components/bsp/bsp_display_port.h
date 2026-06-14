#pragma once
#include <stdbool.h>

// 显示初始化接口：先启动 LVGL，再由 main 在首屏创建完成后打开背光。

// 初始化 BSP 显示和 LVGL 端口，可重复调用。
bool app_display_init(void);
// 打开背光，通常在 UI 首屏创建完成后调用。
void app_display_backlight_on(void);
