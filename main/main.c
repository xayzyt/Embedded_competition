/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */
/* 引入 stdio.h；标准输入输出头文件；常见的 printf、snprintf 等格式化输出接口都在这里声明。 */
#include <stdio.h>
/* 引入 stdint.h；标准整数类型头文件；这里提供 uint8_t、uint32_t 这类位宽固定的整数类型，嵌入式里很常用。 */
#include <stdint.h>
/* 引入 freertos/FreeRTOS.h；FreeRTOS 核心头文件；任务、队列、事件组等内核对象的基础定义都依赖它。 */
#include "freertos/FreeRTOS.h"
/* 引入 freertos/task.h；FreeRTOS 任务头文件；xTaskCreate、vTaskDelay、任务通知等接口主要在这里声明。 */
#include "freertos/task.h"
/* 引入 nvs_flash.h；ESP-IDF 的 NVS Flash 头文件；保存 Wi-Fi 参数、任务配置等掉电不丢失数据时要先初始化它。 */
#include "nvs_flash.h"
/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"
/* 引入 esp_err.h；ESP-IDF 错误码头文件；esp_err_t、ESP_OK、ESP_ERROR_CHECK 等错误处理机制依赖它。 */
#include "esp_err.h"
/* 引入 bsp_display_port.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "bsp_display_port.h"
/* 引入本项目的 app_ui 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ui.h"
/* 引入本项目的 app_camera 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_camera.h"
/* 引入本项目的 app_vision 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_vision.h"
/* 引入本项目的 app_dock_judge 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_dock_judge.h"
/* 引入本项目的 app_ch32_link 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ch32_link.h"
/* 引入本项目的 app_ctrl 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ctrl.h"
/* 引入本项目的 app_task 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_task.h"
/* 引入本项目的 app_cloud 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_cloud.h"
/* 这里把 "main" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "main";
/* 定义宏 APP_TARGET_TAG_ID；这里把这个关键参数单独集中在文件顶部，后面初始化配置时直接引用它。目标 AprilTag 的期望 ID；只有识别到这个编号时，系统才会把它当成“合法来机”继续往下处理。 */
#define APP_TARGET_TAG_ID            (1U)
/* 定义宏 APP_TAG_SIZE_MM；这里把这个关键参数单独集中在文件顶部，后面初始化配置时直接引用它。AprilTag 实物边长（毫米）；距离估算时会把图像里看到的标签大小和这个真实尺寸对应起来。 */
#define APP_TAG_SIZE_MM              (100)
/* 定义宏 APP_DISTANCE_GATE_ENABLE；这里把这个关键参数单独集中在文件顶部，后面初始化配置时直接引用它。是否开启距离门限；打开后，只有目标距离落在安全窗口内才允许继续接驳。 */
#define APP_DISTANCE_GATE_ENABLE     (1)
/* 定义宏 APP_FOCAL_LENGTH_PX；这里把这个关键参数单独集中在文件顶部，后面初始化配置时直接引用它。用于距离换算的等效焦距（像素）；这是把图像尺寸和真实距离联系起来的关键参数。 */
#define APP_FOCAL_LENGTH_PX          (314.0f)
/* 定义宏 APP_MIN_DISTANCE_MM；这里把这个关键参数单独集中在文件顶部，后面初始化配置时直接引用它。允许接驳的最小距离；太近通常容易撞窗体、舱门或托盘机构。 */
#define APP_MIN_DISTANCE_MM          (260)
/* 定义宏 APP_MAX_DISTANCE_MM；这里把这个关键参数单独集中在文件顶部，后面初始化配置时直接引用它。允许接驳的最大距离；太远时目标虽然能看到，但姿态和投递往往已经不稳定。 */
#define APP_MAX_DISTANCE_MM          (420)






/* 这里开始定义函数 app_init_nvs；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_init_nvs(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 nvs_flash_init()；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = nvs_flash_init();

    /* 这里开始判断条件 ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND；只有条件成立，后面的分支代码才会执行。 */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* 这里把 nvs_flash_init() 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ret = nvs_flash_init();
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(ret);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_main；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_main(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "==== SkyAnchor AprilTag + CH32 dock chain start ====");


    /* 调用本项目模块接口 app_init_nvs；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_init_nvs();


    /* 这里开始判断条件 !app_display_init()；只有条件成立，后面的分支代码才会执行。 */
    if (!app_display_init()) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "display init failed");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !app_ui_create()；只有条件成立，后面的分支代码才会执行。 */
    if (!app_ui_create()) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "ui create failed");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_ui_set_status；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_status("dock: booting");

    /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_vision_text("vision: init");

    /* 调用本项目模块接口 app_ui_set_dock_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_dock_text("dock dbg: init");


    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_ch32_link_init(app_ctrl_on_ch32_line, NULL));

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_vision_init());


    /* 这里先定义变量 dock_cfg，类型是 app_dock_judge_config_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_dock_judge_config_t dock_cfg;

    /* 调用本项目模块接口 app_dock_judge_get_default_config；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_dock_judge_get_default_config(&dock_cfg);


    /* 这里把 true 写入 接驳、配置、use、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    dock_cfg.use_target_id = true;

    /* 这里把 APP_TARGET_TAG_ID 写入 接驳、配置、目标、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    dock_cfg.target_tag_id = APP_TARGET_TAG_ID;

    /* 这里把 APP_TAG_SIZE_MM 写入 接驳、配置、标签、大小、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    dock_cfg.tag_size_mm = APP_TAG_SIZE_MM;

    /* 这里把 APP_FOCAL_LENGTH_PX 写入 接驳、配置、焦距、长度、像素；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    dock_cfg.focal_length_px = APP_FOCAL_LENGTH_PX;

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    dock_cfg.use_distance_gate = (APP_DISTANCE_GATE_ENABLE != 0);

    /* 这里把 APP_MIN_DISTANCE_MM 写入 接驳、配置、最小、距离、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    dock_cfg.min_distance_mm = APP_MIN_DISTANCE_MM;

    /* 这里把 APP_MAX_DISTANCE_MM 写入 接驳、配置、最大、距离、毫米；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    dock_cfg.max_distance_mm = APP_MAX_DISTANCE_MM;


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG,
             /* 这里把 %u tag=%ldmm focal=%.1f dist_gate=%d range=[%ld,%ld]", 写入 接驳、配置、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
             "dock cfg: target=%u tag=%ldmm focal=%.1f dist_gate=%d range=[%ld,%ld]",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)dock_cfg.target_tag_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)dock_cfg.tag_size_mm,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (double)dock_cfg.focal_length_px,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             dock_cfg.use_distance_gate,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)dock_cfg.min_distance_mm,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)dock_cfg.max_distance_mm);


    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_dock_judge_init(&dock_cfg));

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_task_init(APP_TARGET_TAG_ID));







    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_cloud_init());


    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_ctrl_init());

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_ctrl_start());


    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_camera_init());

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_vision_start());

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(app_camera_preview_start());


    /* 这里开始判断条件 dock_cfg.use_distance_gate；只有条件成立，后面的分支代码才会执行。 */
    if (dock_cfg.use_distance_gate) {

        /* 调用本项目模块接口 app_ui_set_status；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_set_status("task: configured / dist gate on");
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 调用本项目模块接口 app_ui_set_status；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_set_status("task: configured / z calib");
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_vision_text("task:configured target:1 src:local");


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "system ready");
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
