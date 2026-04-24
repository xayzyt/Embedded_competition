#include "bsp_display_port.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/display.h"
#include "lvgl.h"
static const char *TAG = "display_port";
static lv_indev_t *s_indev = NULL;
static lv_display_t *s_disp = NULL;
bool app_display_init(void)
{
    if (s_disp != NULL) {
        return true;
    }
    ESP_LOGI(TAG, "start display init (custom LVGL cfg)");
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 20,
        .double_buffer = false,
        .flags = {
            .buff_dma = 0,
            .buff_spiram = 1,
            .sw_rotate = 0,
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
    s_indev = bsp_display_get_input_dev();
    if (s_indev == NULL) {
        ESP_LOGW(TAG, "touch input device not found, continue without touch");
    }
    ESP_LOGI(TAG, "display init done");
    return true;
}
void app_display_lock(void)
{
    bsp_display_lock(0);
}
void app_display_unlock(void)
{
    bsp_display_unlock();
}
bool app_display_touch_read(int32_t *x, int32_t *y)
{
    if (s_indev == NULL || x == NULL || y == NULL) {
        return false;
    }
#if LVGL_VERSION_MAJOR >= 9
    if (lv_indev_get_state(s_indev) != LV_INDEV_STATE_PRESSED) {
        return false;
    }
    lv_point_t point = {0, 0};
    lv_indev_get_point(s_indev, &point);
    *x = point.x;
    *y = point.y;
    return true;
#else
    lv_indev_data_t data = {0};
    s_indev->driver.read_cb(&s_indev->driver, &data);
    if (data.state != LV_INDEV_STATE_PRESSED) {
        return false;
    }
    *x = data.point.x;
    *y = data.point.y;
    return true;
#endif
}
