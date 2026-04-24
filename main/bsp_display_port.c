/*
 * bsp_display_port.c - 显示 BSP 适配封装模块（详细注释版）
 *
 * 这个文件对乐鑫官方 BSP 显示接口做了一层项目级封装。
 * main.c 不直接调用 bsp_display_start_with_config()，而是调用 app_display_init()，
 * 这样后续如果你换屏幕、改 LVGL buffer、改背光或触摸初始化，只需要改这个文件，不需要改业务层。
 *
 * 同时本文件提供 app_display_lock()/unlock() 和触摸读取函数，方便 app_ui.c 等模块安全访问 LVGL。
 */

#include "bsp_display_port.h"                      // 项目自定义 BSP 封装头文件，用来屏蔽底层显示板级差异。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "bsp/esp32_p4_function_ev_board.h"        // ESP32-P4 Function EV Board 官方 BSP 板级定义。
#include "bsp/display.h"                           // BSP 显示接口和分辨率宏，例如 BSP_LCD_H_RES/BSP_LCD_V_RES。
#include "lvgl.h"                                  // LVGL 图形库主头文件，提供控件、样式、画布、颜色等 API。
static const char *TAG = "display_port";                         // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
static lv_indev_t *s_indev = NULL;                               // 模块级静态变量 s_indev，只在本文件内部使用，避免被其他文件直接修改。
static lv_display_t *s_disp = NULL;                              // 模块级静态变量 s_disp，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 初始化屏幕、LVGL、显示缓冲和触摸输入设备。
 */
bool app_display_init(void)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_disp != NULL) {
        return true;
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "start display init (custom LVGL cfg)");

    /*
     * BSP 显示配置。
     *
     * buffer_size = 一次刷新 20 行，既能降低内存占用，又能满足 LVGL 刷屏；
     * double_buffer=false 表示只使用单显示缓冲；
     * buff_spiram=1 表示缓冲放 PSRAM，减少内部 SRAM 压力。
     */
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

    /*
     * 增大 LVGL port 任务栈。
     * 当前 UI 叠加了 canvas、HUD、标签等对象，栈太小容易在复杂刷新时出问题。
     */
    cfg.lvgl_port_cfg.task_stack = 8192;

    /*
     * 使用 BSP 提供的 MIPI-DSI lane bit rate。
     */
    cfg.hw_cfg.dsi_bus.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;

    /*
     * 启动显示和 LVGL port。
     */
    s_disp = bsp_display_start_with_config(&cfg);
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_disp == NULL) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return false;
    }

    /*
     * 显示初始化成功后打开背光。
     */
    bsp_display_backlight_on();

    /*
     * 获取触摸输入设备。
     * 触摸不是系统必需项，即使没有触摸，也允许 UI 和摄像头预览继续运行。
     */
    s_indev = bsp_display_get_input_dev();
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_indev == NULL) {
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "touch input device not found, continue without touch");
    }
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "display init done");
    return true;
}
/*
 * 封装 BSP 的 LVGL 锁，其他任务操作 LVGL 前必须先加锁。
 */
void app_display_lock(void)
{
    // 加 LVGL/BSP 显示锁，防止多个任务同时操作 UI 控件。
    bsp_display_lock(0);
}
/*
 * 释放 LVGL 锁，让显示刷新任务可以继续运行。
 */
void app_display_unlock(void)
{
    // 释放 LVGL/BSP 显示锁。
    bsp_display_unlock();
}
/*
 * 读取当前触摸点坐标，兼容 LVGL v8/v9 输入设备接口差异。
 */
bool app_display_touch_read(int32_t *x, int32_t *y)
{
    // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
    if (s_indev == NULL || x == NULL || y == NULL) {
        return false;
    }
#if LVGL_VERSION_MAJOR >= 9
    /*
     * LVGL v9 使用 lv_indev_get_state/lv_indev_get_point 读取输入设备。
     */
    if (lv_indev_get_state(s_indev) != LV_INDEV_STATE_PRESSED) {
        return false;
    }
    lv_point_t point = {0, 0};
    lv_indev_get_point(s_indev, &point);
    *x = point.x;
    *y = point.y;
    return true;
#else
    /*
     * LVGL v8 需要通过 driver.read_cb 主动读取输入数据。
     */
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
