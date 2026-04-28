/*
 * bsp_display_port.c - 显示 BSP 适配封装模块（详细注释版）
 *
 * 这个文件对乐鑫官方 BSP 显示接口做了一层项目级封装。
 * main.c 不直接调用 bsp_display_start_with_config()，而是调用 app_display_init()，
 * 这样后续如果你换屏幕、改 LVGL buffer、改背光或触摸初始化，只需要改这个文件，不需要改业务层。
 *
 * UI 和摄像头模块会直接使用 BSP/LVGL 的锁接口；这里保留显示初始化入口，
 * 让业务层不依赖具体屏幕启动配置。
 */

#include "bsp_display_port.h"                      // 项目自定义 BSP 封装头文件，用来屏蔽底层显示板级差异。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "bsp/esp32_p4_function_ev_board.h"        // ESP32-P4 Function EV Board 官方 BSP 板级定义。
#include "bsp/display.h"                           // BSP 显示接口和分辨率宏，例如 BSP_LCD_H_RES/BSP_LCD_V_RES。
#include "lvgl.h"                                  // LVGL 图形库主头文件，提供控件、样式、画布、颜色等 API。
static const char *TAG = "display_port";                         // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
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
     * 当前工程开启了 avoid-tear + full-refresh，需要全屏大小的 LVGL buffer。
     * 1024x600 RGB565 下，一个全屏 buffer 约 1.17 MB，双缓冲约 2.34 MB；
     * 所以优先放 PSRAM，避免占用紧张的内部 DMA 内存。
     *
     * buff_dma 只是“给 LVGL draw buffer 追加 MALLOC_CAP_DMA”的申请条件。
     * 在当前 MIPI-DSI avoid-tear 路径里，真正给 LCD 扫描的 framebuffer
     * 由 esp_lcd DPI 驱动创建，LVGL port 会直接取这些 framebuffer 使用。
     * 因此这里不强制 LVGL buffer 也带 DMA 属性，打开它通常没有收益，
     * 还可能增加大块 DMA-capable 内存分配失败的概率。
     */
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

    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "display init done");
    return true;
}
