#include "bsp_display_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/display.h"
#include "lvgl.h"

// 显示 BSP 封装：统一配置 LVGL 端口和 MIPI DSI 显示参数。
static const char *TAG = "display_port";
static lv_display_t *s_disp = NULL;

// 初始化只执行一次，后续重复调用直接复用已创建的显示对象。
bool app_display_init(void)
{
    if (s_disp != NULL)
    {
        return true;
    }
    ESP_LOGI(TAG, "display init begin (custom LVGL cfg)");
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
    // 屏幕分辨率较高，LVGL 任务栈调大并在配置允许时放入 PSRAM。
    cfg.lvgl_port_cfg.task_stack = 8192;
#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    cfg.lvgl_port_cfg.task_stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
    cfg.hw_cfg.dsi_bus.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
    ESP_LOGI(TAG, "bsp_display_start_with_config begin");
    s_disp = bsp_display_start_with_config(&cfg);
    if (s_disp == NULL)
    {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return false;
    }
    ESP_LOGI(TAG, "display init done");
    return true;
}
// 背光延后到 UI 创建完成后开启，避免启动过程中短暂白屏。
void app_display_backlight_on(void)
{
    bsp_display_backlight_on();
}
