#pragma once

/*
 * 项目级显示 BSP 门面。
 * 让 main.c 不直接依赖板级 LVGL 和背光初始化细节。
 */

#include <stdbool.h>

/* 初始化 MIPI DSI、LVGL 端口和触摸输入。 */
bool app_display_init(void);

/* 首帧 UI 准备好后再打开背光，减少启动白屏。 */
void app_display_backlight_on(void);
