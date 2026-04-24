/*
 * main.c - SkyAnchor 主程序入口文件（详细注释版）
 *
 * 这个文件相当于整个 ESP32-P4 端工程的“启动总控”。
 * 它不直接实现摄像头、LVGL 界面、AprilTag 识别、CH32 通信、状态机、云端 MQTT 等具体功能，
 * 而是负责按照正确顺序把这些模块初始化并启动起来。
 *
 * 对你当前的“阳台式无人机物流智能接收舱 / SkyAnchor”项目来说，
 * app_main() 的启动链路大致是：
 *
 * 1. 初始化 NVS 非易失性存储；
 * 2. 初始化屏幕显示底层；
 * 3. 创建 LVGL UI 页面；
 * 4. 初始化 ESP32-P4 与 CH32V203 的通信链路；
 * 5. 初始化 AprilTag / 视觉识别模块；
 * 6. 配置无人机接驳判定参数，例如目标标签 ID、标签尺寸、距离阈值；
 * 7. 初始化任务管理、云端通信、控制状态机；
 * 8. 启动摄像头、视觉识别和本地预览；
 * 9. 更新 UI 状态，提示系统已经准备完成。
 *
 * 注意：
 * - 本文件只添加详细中文注释，不改变原有业务逻辑。
 * - 如果后续要加入电机、推杆、称重、红外等外设，通常不要全部写在 main.c 里，
 *   而是继续保持模块化风格，例如 app_motor.c / app_weight.c / app_ir.c，
 *   然后在 app_main() 中按顺序调用它们的 init/start 函数。
 */

#include <stdio.h>                 // C 标准输入输出头文件。当前 main.c 中没有直接使用 printf，但 ESP-IDF 工程里经常保留它，方便后续调试打印。
#include <stdint.h>                // C 标准整数类型头文件，提供 uint8_t、uint16_t、uint32_t 等固定宽度整数类型。

#include "freertos/FreeRTOS.h"     // FreeRTOS 基础头文件，提供 Tick、任务调度、队列、信号量等 RTOS 基础定义。
#include "freertos/task.h"         // FreeRTOS 任务相关头文件，提供 xTaskCreate、vTaskDelay 等任务 API。本文件未直接创建任务，但被后续模块可能间接依赖。

#include "nvs_flash.h"             // ESP-IDF 的 NVS Flash 头文件。NVS 用于保存 Wi-Fi 配置、设备参数、校准数据等非易失性数据。
#include "esp_log.h"               // ESP-IDF 日志系统头文件，提供 ESP_LOGI、ESP_LOGE 等日志打印宏。
#include "esp_err.h"               // ESP-IDF 错误码和错误检查头文件，提供 esp_err_t、ESP_ERROR_CHECK 等。

#include "bsp_display_port.h"      // 板级显示初始化接口。通常封装了 MIPI-DSI 屏幕、背光、触摸、LVGL port 等底层显示相关初始化。
#include "app_ui.h"                // 项目的 UI 模块接口。负责创建 LVGL 界面，并提供修改状态文本、视觉文本、调试文本等函数。
#include "app_camera.h"            // 项目的摄像头模块接口。负责 MIPI-CSI 摄像头初始化、图像采集以及预览启动等。
#include "app_vision.h"            // 项目的视觉模块接口。负责 AprilTag / 图像识别流程的初始化和启动。
#include "app_dock_judge.h"        // 接驳判定模块接口。根据 tag ID、距离、稳定性等信息判断是否允许无人机接驳。
#include "app_ch32_link.h"         // ESP32-P4 与 CH32V203 副控通信模块接口，通常通过 UART/SPI 等方式收发控制帧。
#include "app_ctrl.h"              // 主控制状态机模块接口。负责接驳流程、CH32 回调处理、开舱/关舱等高层控制逻辑。
#include "app_task.h"              // 项目任务管理模块接口。用于创建或初始化工程中的后台任务、队列、事件等。
#include "app_cloud.h"             // 云端通信模块接口。通常负责 Wi-Fi、MQTT、状态上报、小程序/服务器通信等。

/*
 * TAG 是 ESP-IDF 日志系统使用的模块标签。
 *
 * ESP_LOGI(TAG, "xxx") 打印出来时会带上 [main] 这样的来源标识，
 * 方便你在串口日志中区分这条日志来自 main.c，还是 app_camera.c / app_ui.c 等其他模块。
 */
static const char *TAG = "main";

/*
 * 以下宏是无人机视觉接驳判定的核心参数。
 *
 * 这些参数会在 app_main() 中写入 app_dock_judge_config_t 配置结构体，
 * 最终交给 app_dock_judge_init() 使用。
 *
 * 你可以把它理解成：main.c 负责给“接驳判定模块”设置规则，
 * app_dock_judge.c 负责真正按照这些规则去判断。
 */

#define APP_TARGET_TAG_ID            (1U)       // 目标 AprilTag ID。只有识别到 ID=1 的标签时，才认为是允许接驳的目标无人机。
#define APP_TAG_SIZE_MM              (100)      // AprilTag 标签实际边长，单位 mm。这里是 100mm，用于根据图像大小估算距离。
#define APP_DISTANCE_GATE_ENABLE     (1)        // 是否启用距离门限。1 表示启用，0 表示关闭距离范围判断。
#define APP_FOCAL_LENGTH_PX          (314.0f)   // 摄像头等效焦距，单位为像素 px。用于根据标签成像尺寸计算无人机与窗口的距离。
#define APP_MIN_DISTANCE_MM          (260)      // 允许接驳的最小距离，单位 mm。距离太近可能有碰撞风险。
#define APP_MAX_DISTANCE_MM          (420)      // 允许接驳的最大距离，单位 mm。距离太远说明无人机还没有进入安全接驳区域。

/*
 * app_init_nvs() - 初始化 ESP-IDF 的 NVS 非易失性存储
 *
 * NVS 的作用：
 * - 保存 Wi-Fi 配网信息；
 * - 保存 MQTT 参数、设备 ID、用户配置；
 * - 保存一些需要断电后仍然保留的校准值；
 * - ESP-IDF 内部组件也可能依赖 NVS。
 *
 * 为什么 main.c 一开始就初始化 NVS？
 * 因为后续 app_cloud_init() 这类联网模块可能会读取 Wi-Fi 或设备配置，
 * 如果 NVS 没有先准备好，后面模块就可能初始化失败。
 */
static void app_init_nvs(void)
{
    /*
     * nvs_flash_init() 是 ESP-IDF 官方提供的 NVS 初始化函数。
     *
     * 返回值 ret 的类型是 esp_err_t：
     * - ESP_OK：初始化成功；
     * - ESP_ERR_NVS_NO_FREE_PAGES：NVS 分区没有空闲页，通常是分区内容异常或升级后布局变化；
     * - ESP_ERR_NVS_NEW_VERSION_FOUND：Flash 中的 NVS 数据版本比当前固件支持的版本更新；
     * - 其他错误：说明 NVS 初始化失败。
     */
    esp_err_t ret = nvs_flash_init();

    /*
     * 这里处理两个常见异常：
     *
     * 1. ESP_ERR_NVS_NO_FREE_PAGES
     *    说明 NVS 分区空间不可用，常见于反复烧录、分区表变化或旧数据残留。
     *
     * 2. ESP_ERR_NVS_NEW_VERSION_FOUND
     *    说明 Flash 里已有的 NVS 数据版本和当前固件不兼容。
     *
     * 对于开发阶段，最直接可靠的处理方式是擦除 NVS 分区，然后重新初始化。
     * 注意：擦除后，里面保存的 Wi-Fi 配置、设备参数等都会丢失。
     */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());  // 擦除整个 NVS 分区；如果擦除失败，ESP_ERROR_CHECK 会打印错误并终止程序。
        ret = nvs_flash_init();              // 擦除后再次初始化 NVS，此时通常应该成功。
    }

    /*
     * ESP_ERROR_CHECK(ret) 是 ESP-IDF 常用的错误检查宏。
     *
     * 如果 ret == ESP_OK：程序继续运行；
     * 如果 ret != ESP_OK：打印错误信息，并触发 abort，方便开发阶段尽早发现问题。
     *
     * NVS 属于系统基础组件，初始化失败后继续运行意义不大，
     * 所以这里使用 ESP_ERROR_CHECK 是合理的。
     */
    ESP_ERROR_CHECK(ret);
}

/*
 * app_main() - ESP-IDF 应用程序入口函数
 *
 * 在普通 C 语言程序里，入口函数通常是 main()。
 * 但在 ESP-IDF 中，系统启动、FreeRTOS 调度器、底层硬件初始化由框架完成，
 * 用户应用从 app_main() 开始执行。
 *
 * 可以把 app_main() 理解成：
 * “ESP32-P4 上电启动后，你的 SkyAnchor 项目从这里开始搭积木”。
 */
void app_main(void)
{
    /*
     * 打印系统启动日志。
     *
     * ESP_LOGI 表示 Info 级别日志，用于输出普通运行信息。
     * 这里的日志告诉你：SkyAnchor 的 AprilTag + CH32 接驳链路开始启动。
     */
    ESP_LOGI(TAG, "==== SkyAnchor AprilTag + CH32 dock chain start ====");

    /*
     * 第一步：初始化 NVS。
     *
     * 这个步骤必须尽量靠前，因为后续联网、云端、设备配置等模块可能依赖 NVS。
     */
    app_init_nvs();

    /*
     * 第二步：初始化显示底层。
     *
     * app_display_init() 通常来自 bsp_display_port.h 对应的板级支持包，
     * 可能会完成以下工作：
     * - 初始化 MIPI-DSI LCD；
     * - 初始化屏幕背光 PWM；
     * - 初始化触摸芯片；
     * - 初始化 LVGL 显示缓冲区；
     * - 创建 LVGL tick / flush 相关机制。
     *
     * 如果显示初始化失败，后续 UI 无法创建，所以这里直接打印错误并 return。
     */
    if (!app_display_init()) {
        ESP_LOGE(TAG, "display init failed");  // ESP_LOGE 是 Error 级别日志，表示出现严重问题。
        return;                                // 显示失败后不继续启动，避免后续 UI 模块访问无效显示资源。
    }

    /*
     * 第三步：创建 LVGL 用户界面。
     *
     * app_ui_create() 通常会创建主页面、状态标签、摄像头画面区域、按钮、调试信息等控件。
     * 只有 display 初始化成功后，才能创建 UI。
     */
    if (!app_ui_create()) {
        ESP_LOGE(TAG, "ui create failed");     // UI 创建失败时打印错误。
        return;                                // UI 没有创建成功，后续 set_status 等函数可能无法正常工作，因此直接退出。
    }

    /*
     * UI 创建完成后，先显示几个初始状态。
     *
     * 这些文本能让屏幕在系统启动过程中有反馈，
     * 不会让用户面对一块“没有变化”的屏幕。
     */
    app_ui_set_status("dock: booting");        // 设置主状态栏文本：接驳系统正在启动。
    app_ui_set_vision_text("vision: init");    // 设置视觉识别区域文本：视觉模块正在初始化。
    app_ui_set_dock_text("dock dbg: init");    // 设置接驳调试区域文本：接驳调试信息初始化。

    /*
     * 第四步：初始化 ESP32-P4 与 CH32V203 的通信链路。
     *
     * app_ch32_link_init() 的第一个参数 app_ctrl_on_ch32_line 是回调函数。
     *
     * 含义：
     * - CH32 副控通过 UART/SPI 发来一行数据或一帧数据；
     * - app_ch32_link 模块负责底层接收；
     * - 收到后把数据交给 app_ctrl_on_ch32_line()；
     * - app_ctrl 模块再根据 CH32 的反馈更新状态机，例如“推杆已到位”“货物已检测”“电机异常”等。
     *
     * 第二个参数 NULL 通常表示没有传入用户自定义上下文指针。
     */
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));

    /*
     * 第五步：初始化视觉识别模块。
     *
     * app_vision_init() 一般负责准备 AprilTag 检测器、图像处理缓冲区、识别任务资源等。
     * 注意：这里通常只是 init，还不一定开始持续识别；真正开始识别在后面的 app_vision_start()。
     */
    ESP_ERROR_CHECK(app_vision_init());

    /*
     * 第六步：创建并配置接驳判定参数结构体。
     *
     * app_dock_judge_config_t 是接驳判定模块的配置结构体。
     * 它保存“什么条件下允许开舱接驳”的规则。
     */
    app_dock_judge_config_t dock_cfg;

    /*
     * 先获取默认配置。
     *
     * 这样做的好处是：
     * - 结构体里未来如果增加字段，不容易漏初始化；
     * - 默认参数集中维护在 app_dock_judge.c 里；
     * - main.c 只覆盖自己关心的关键参数。
     */
    app_dock_judge_get_default_config(&dock_cfg);

    /*
     * use_target_id = true 表示启用目标 AprilTag ID 检查。
     *
     * 启用后，系统不会看到任意 AprilTag 都开舱，
     * 而是只允许 target_tag_id 指定的标签通过。
     */
    dock_cfg.use_target_id = true;

    /*
     * 设置目标标签 ID。
     *
     * 当前 APP_TARGET_TAG_ID = 1，
     * 所以只有识别到 ID 为 1 的 AprilTag 时，才认为是目标无人机。
     */
    dock_cfg.target_tag_id = APP_TARGET_TAG_ID;

    /*
     * 设置 AprilTag 标签实际尺寸。
     *
     * 视觉测距通常需要知道“真实世界中标签边长是多少”。
     * 例如这里是 100mm，摄像头看到标签在图像中占多少像素，
     * 再结合焦距，就可以估算无人机距离窗口多远。
     */
    dock_cfg.tag_size_mm = APP_TAG_SIZE_MM;

    /*
     * 设置摄像头等效焦距。
     *
     * focal_length_px 是像素单位的焦距，不是毫米单位的镜头焦距。
     * 它通常来自标定或粗略估算。
     * 如果测距偏差较大，后续需要重新标定这个值。
     */
    dock_cfg.focal_length_px = APP_FOCAL_LENGTH_PX;

    /*
     * 设置是否启用距离门限。
     *
     * APP_DISTANCE_GATE_ENABLE != 0 的结果是 true，表示启用。
     * 启用后，无人机必须在 min_distance_mm 和 max_distance_mm 之间，
     * 才允许进入接驳流程。
     */
    dock_cfg.use_distance_gate = (APP_DISTANCE_GATE_ENABLE != 0);

    /*
     * 设置允许接驳的最小距离。
     *
     * 如果无人机太靠近窗口或托盘，可能撞到机构，
     * 因此小于这个距离时应该拒绝开舱或继续观察。
     */
    dock_cfg.min_distance_mm = APP_MIN_DISTANCE_MM;

    /*
     * 设置允许接驳的最大距离。
     *
     * 如果无人机离窗口太远，托盘伸出去也未必能安全接住货物，
     * 因此大于这个距离时也不应该触发接驳。
     */
    dock_cfg.max_distance_mm = APP_MAX_DISTANCE_MM;

    /*
     * 打印接驳配置参数，方便串口调试。
     *
     * 这条日志很重要：
     * 当你发现系统“不允许接驳”时，可以先看这里确认参数是否符合预期。
     *
     * 格式说明：
     * - target：目标 AprilTag ID；
     * - tag：标签实际边长；
     * - focal：像素焦距；
     * - dist_gate：是否启用距离门限；
     * - range：允许距离范围。
     */
    ESP_LOGI(TAG,
             "dock cfg: target=%u tag=%ldmm focal=%.1f dist_gate=%d range=[%ld,%ld]",
             (unsigned)dock_cfg.target_tag_id,      // 转成 unsigned，匹配 %u，避免 printf 类型不匹配。
             (long)dock_cfg.tag_size_mm,            // 转成 long，匹配 %ld，兼容不同平台整数宽度。
             (double)dock_cfg.focal_length_px,      // printf 中 float 会提升为 double，这里显式转换更清晰。
             dock_cfg.use_distance_gate,            // bool 类型作为整数打印，0 表示 false，1 表示 true。
             (long)dock_cfg.min_distance_mm,        // 打印最小允许距离。
             (long)dock_cfg.max_distance_mm);       // 打印最大允许距离。

    /*
     * 第七步：初始化接驳判定模块。
     *
     * 这里把刚才配置好的 dock_cfg 传进去。
     * 后续视觉模块识别到 AprilTag 后，接驳判定模块会根据这些参数输出结果：
     * - 是否是目标 ID；
     * - 距离是否在范围内；
     * - 是否满足接驳条件。
     */
    ESP_ERROR_CHECK(app_dock_judge_init(&dock_cfg));

    /*
     * 第八步：初始化项目任务模块。
     *
     * APP_TARGET_TAG_ID 传进去，说明任务模块也需要知道当前目标无人机的标签 ID。
     * app_task_init() 可能会创建一些后台任务、事件队列，或者把视觉结果转发给状态机。
     */
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));

    /*
     * 第九步：初始化云端模块。
     *
     * app_cloud_init() 一般负责 Wi-Fi / MQTT / 云端状态同步等。
     * 在你的项目里，它可以用于：
     * - 上报当前接收舱状态；
     * - 上报无人机识别结果；
     * - 通知小程序“已投递”；
     * - 接收远程开舱/关舱等指令。
     */
    ESP_ERROR_CHECK(app_cloud_init());

    /*
     * 第十步：初始化主控制状态机。
     *
     * app_ctrl_init() 一般负责准备接驳流程状态机。
     * 例如：待机、识别中、允许接驳、托盘伸出、货物检测、回收关舱、异常处理等。
     */
    ESP_ERROR_CHECK(app_ctrl_init());

    /*
     * 第十一步：启动主控制状态机。
     *
     * init 和 start 分开是比较好的模块化习惯：
     * - init：创建资源、初始化变量；
     * - start：真正开始运行任务、定时器或状态机。
     */
    ESP_ERROR_CHECK(app_ctrl_start());

    /*
     * 第十二步：初始化摄像头模块。
     *
     * app_camera_init() 通常会初始化 MIPI-CSI 摄像头、分配图像缓冲区、配置分辨率和像素格式等。
     * 注意：摄像头初始化放在控制模块之后也可以，因为摄像头启动后很快就可能产生图像数据。
     */
    ESP_ERROR_CHECK(app_camera_init());

    /*
     * 第十三步：启动视觉识别模块。
     *
     * app_vision_start() 通常会启动识别任务，开始从摄像头图像中查找 AprilTag。
     * 视觉模块启动后，可能会周期性更新 UI，也可能会把识别结果发给接驳判定或状态机。
     */
    ESP_ERROR_CHECK(app_vision_start());

    /*
     * 第十四步：启动摄像头预览。
     *
     * app_camera_preview_start() 通常会把摄像头画面送到 LVGL 或显示缓冲区，
     * 让屏幕上可以看到实时图像。
     *
     * 这一步和 app_vision_start() 的区别：
     * - vision_start 关注“识别”；
     * - preview_start 关注“显示”。
     */
    ESP_ERROR_CHECK(app_camera_preview_start());

    /*
     * 根据距离门限是否启用，更新 UI 主状态。
     *
     * 如果启用了距离门限，显示 dist gate on，说明系统会检查无人机距离是否在允许范围内。
     * 如果没有启用距离门限，显示 z calib，提示可能需要依赖 Z 方向标定或其他测距策略。
     */
    if (dock_cfg.use_distance_gate) {
        app_ui_set_status("task: configured / dist gate on");  // 距离门限开启，系统已配置完成。
    } else {
        app_ui_set_status("task: configured / z calib");       // 距离门限关闭，提示使用 Z 标定相关逻辑。
    }

    /*
     * 更新视觉状态文本。
     *
     * 这里的含义是：
     * - task:configured：任务已经配置完成；
     * - target:1：当前目标 AprilTag ID 是 1；
     * - src:local：识别/判定来源是本地端侧，而不是云端。
     *
     * 建议后续如果 APP_TARGET_TAG_ID 改成变量，可以把这里也改成格式化字符串，避免写死 target:1。
     */
    app_ui_set_vision_text("task:configured target:1 src:local");

    /*
     * 最后打印系统准备完成日志。
     *
     * 如果串口里能看到 system ready，说明 main.c 这一整条启动链路已经执行完毕。
     * 之后系统运行主要依靠各个模块内部创建的 FreeRTOS 任务、回调函数、状态机和事件队列。
     */
    ESP_LOGI(TAG, "system ready");
}
