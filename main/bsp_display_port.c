/*
 * bsp_display_port.c - 显示 BSP 适配封装模块
 *
 * 这个文件对乐鑫官方 BSP 显示接口做了一层项目级封装。
 * main.c 不直接调用 bsp_display_start_with_config()，而是调用 app_display_init()，
 * 这样后续如果你换屏幕、改 LVGL buffer、改背光或触摸初始化，只需要改这个文件，不需要改业务层。
 *
 * UI 和摄像头模块会直接使用 BSP/LVGL 的锁接口；这里保留显示初始化入口，
 * 让业务层不依赖具体屏幕启动配置。
 */

#include "bsp_display_port.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/display.h"
#include "lvgl.h"

static const char *TAG = "display_port";
static lv_display_t *s_disp = NULL;
bool app_display_init(void)
{
    if (s_disp != NULL) {
        return true;
    }
    ESP_LOGD(TAG, "start display init (custom LVGL cfg)");
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        },
    };
    cfg.lvgl_port_cfg.task_stack = 8192;
    cfg.hw_cfg.dsi_bus.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
    s_disp = bsp_display_start_with_config(&cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return false;
    }
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "display init done");
    return true;
}
