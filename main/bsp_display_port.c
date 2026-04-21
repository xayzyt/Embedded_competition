/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */





























/* 引入 bsp_display_port.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "bsp_display_port.h"

/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"
/* 引入 bsp/esp32_p4_function_ev_board.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "bsp/esp32_p4_function_ev_board.h"
/* 引入 bsp/display.h；BSP 显示相关头文件；屏幕句柄、显示锁、面板操作等接口一般在这里。 */
#include "bsp/display.h"
/* 引入 lvgl.h；LVGL 图形库总头文件；创建控件、刷新界面、管理样式都从这里进入。 */
#include "lvgl.h"









/* 这里把 "display_port" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "display_port";

/* 这里把 NULL 写入 static、lv、indev、t、s、indev；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_indev_t *s_indev = NULL;

/* 这里把 NULL 写入 static、lv、显示、t、s、disp；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_display_t *s_disp = NULL;






/* 这里开始定义函数 app_display_init；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_display_init(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_disp != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_disp != NULL) {

        /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "start display init (custom LVGL cfg)");

    /* 这里定义变量 cfg，类型是 bsp_display_cfg_t，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
    bsp_display_cfg_t cfg = {

        /* 这里把 ESP_LVGL_PORT_INIT_CONFIG(), 写入 lvgl、端口、配置；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        /* 这里把 BSP_LCD_H_RES * 20, 写入 缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .buffer_size = BSP_LCD_H_RES * 20,
        /* 这里把 false, 写入 double、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .double_buffer = false,
        /* 这里把 { 写入 flags；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .flags = {
            /* 这里把 0, 写入 buff、dma；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .buff_dma = 0,
            /* 这里把 1, 写入 buff、spiram；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .buff_spiram = 1,
            /* 这里把 0, 写入 sw、rotate；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .sw_rotate = 0,
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        },
    /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
    };


    /* 这里把 8192 写入 配置、lvgl、端口、配置、任务、stack；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    cfg.lvgl_port_cfg.task_stack = 8192;

    /* 这里把 BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS 写入 配置、hw、配置、dsi、bus、lane、bit、rate、mbps；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    cfg.hw_cfg.dsi_bus.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;


    /* 这里把 bsp_display_start_with_config(&cfg) 写入 s、disp；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_disp = bsp_display_start_with_config(&cfg);

    /* 这里开始判断条件 s_disp == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_disp == NULL) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用板级支持包接口 bsp_display_backlight_on；这类函数会把开发板原理图上的屏幕、触摸、电源等资源封装起来。 */
    bsp_display_backlight_on();


    /* 这里把 bsp_display_get_input_dev() 写入 s、indev；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_indev = bsp_display_get_input_dev();

    /* 这里开始判断条件 s_indev == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_indev == NULL) {

        /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
        ESP_LOGW(TAG, "touch input device not found, continue without touch");
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "display init done");

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_display_lock；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_display_lock(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 拿到显示锁；因为 LVGL 不是天然线程安全，所以跨任务操作界面前要先加锁。 */
    bsp_display_lock(0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 这里开始定义函数 app_display_unlock；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_display_unlock(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_display_touch_read；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_display_touch_read(int32_t *x, int32_t *y)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_indev == NULL || x == NULL || y == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_indev == NULL || x == NULL || y == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if LVGL_VERSION_MAJOR >= 9

    /* 这里开始判断条件 lv_indev_get_state(s_indev) != LV_INDEV_STATE_PRESSED；只有条件成立，后面的分支代码才会执行。 */
    if (lv_indev_get_state(s_indev) != LV_INDEV_STATE_PRESSED) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里定义变量 point，类型是 lv_point_t，并且在声明时就把初值设成 {0, 0}；这样后面第一次使用它时就是一个确定状态。 */
    lv_point_t point = {0, 0};

    /* 调用 LVGL 图形库接口 lv_indev_get_point；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_indev_get_point(s_indev, &point);
    /* 这里把 point.x 写入 x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *x = point.x;
    /* 这里把 point.y 写入 y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *y = point.y;

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 进入条件编译的兜底分支；前面的编译条件都不满足时会走这里。 */
#else

    /* 这里定义变量 data，类型是 lv_indev_data_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    lv_indev_data_t data = {0};

    /* 调用函数 read_cb；从名字看，它承担的职责和“read、cb”有关，后续行为取决于这个接口的返回结果或副作用。 */
    s_indev->driver.read_cb(&s_indev->driver, &data);

    /* 这里开始判断条件 data.state != LV_INDEV_STATE_PRESSED；只有条件成立，后面的分支代码才会执行。 */
    if (data.state != LV_INDEV_STATE_PRESSED) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
    /* 这里把 data.point.x 写入 x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *x = data.point.x;
    /* 这里把 data.point.y 写入 y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *y = data.point.y;

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
