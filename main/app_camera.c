/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */












/* 引入本项目的 app_camera 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_camera.h"

/* 引入 stdbool.h；标准布尔类型头文件；这里把 true/false 和 bool 定义好，方便表达开关状态。 */
#include <stdbool.h>
/* 引入 stdint.h；标准整数类型头文件；这里提供 uint8_t、uint32_t 这类位宽固定的整数类型，嵌入式里很常用。 */
#include <stdint.h>
/* 引入 string.h；标准字符串/内存处理头文件；memcpy、memset、strcmp 等基础接口都来自这里。 */
#include <string.h>
/* 引入 unistd.h；POSIX 基础接口头文件；常见于 sleep/usleep 一类延时或系统调用封装。 */
#include <unistd.h>

/* 引入 freertos/FreeRTOS.h；FreeRTOS 核心头文件；任务、队列、事件组等内核对象的基础定义都依赖它。 */
#include "freertos/FreeRTOS.h"
/* 引入 freertos/task.h；FreeRTOS 任务头文件；xTaskCreate、vTaskDelay、任务通知等接口主要在这里声明。 */
#include "freertos/task.h"
/* 引入 sdkconfig.h；编译期配置头文件；menuconfig 里配置出的宏会在这里展开。 */
#include "sdkconfig.h"
/* 引入 esp_err.h；ESP-IDF 错误码头文件；esp_err_t、ESP_OK、ESP_ERROR_CHECK 等错误处理机制依赖它。 */
#include "esp_err.h"
/* 引入 esp_heap_caps.h；ESP-IDF 能力堆分配头文件；可以按是否在 PSRAM、是否 DMA 可访问来申请内存。 */
#include "esp_heap_caps.h"
/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"
/* 引入 esp_cache.h；ESP-IDF cache 接口头文件；当 CPU、DMA、LCD 共用外部内存时，往往需要做 cache 同步。 */
#include "esp_cache.h"
/* 引入 esp_private/esp_cache_private.h；ESP-IDF 内部 cache 细节头文件；一般是为了查询对齐要求或做更底层的 cache 管理。 */
#include "esp_private/esp_cache_private.h"

/* 引入 lvgl.h；LVGL 图形库总头文件；创建控件、刷新界面、管理样式都从这里进入。 */
#include "lvgl.h"
/* 引入 bsp/esp-bsp.h；乐鑫 BSP 总头文件；板级资源封装通常会从这里统一引入。 */
#include "bsp/esp-bsp.h"
/* 引入 bsp/display.h；BSP 显示相关头文件；屏幕句柄、显示锁、面板操作等接口一般在这里。 */
#include "bsp/display.h"
/* 引入本项目的 app_video 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_video.h"
/* 引入本项目的 app_vision 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_vision.h"















/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if SOC_PPA_SUPPORTED
/* 引入 driver/ppa.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "driver/ppa.h"
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif







































































































/* 定义宏 CAMERA_NUM_BUFS；这里把“相机、NUM、BUFS”集中写成常量 4，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CAMERA_NUM_BUFS          4
/* 定义宏 STAGE_NUM_BUFS；这里把“STAGE、NUM、BUFS”集中写成常量 3，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define STAGE_NUM_BUFS           3
/* 定义宏 ALIGN_UP；这里把“ALIGN、UP”集中写成常量 (num, align)     (((num) + ((align) - 1)) & ~((align) - 1))，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define ALIGN_UP(num, align)     (((num) + ((align) - 1)) & ~((align) - 1))
/* 定义宏 DISPLAY_TASK_STACK_SIZE；这里把“显示、任务、STACK、大小”集中写成常量 (6 * 1024)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define DISPLAY_TASK_STACK_SIZE  (6 * 1024)
/* 定义宏 DISPLAY_TASK_PRIORITY；这里把“显示、任务、PRIORITY”集中写成常量 7，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define DISPLAY_TASK_PRIORITY    7
/* 定义宏 VISION_SAMPLE_INTERVAL；这里把“视觉、SAMPLE、INTERVAL”集中写成常量 6，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_SAMPLE_INTERVAL   6



/* 这里把 "app_camera" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_camera";








/* 这里定义变量 s_camera_inited，类型是 static bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
static bool s_camera_inited = false;

/* 这里定义变量 s_preview_running，类型是 static bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
static bool s_preview_running = false;

/* 这里定义变量 s_video_fd，类型是 static int，并且在声明时就把初值设成 -1；这样后面第一次使用它时就是一个确定状态。 */
static int s_video_fd = -1;


/* 这里把 NULL 写入 static、lv、obj、t、s、相机、canvas；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_camera_canvas = NULL;

/* 这里定义变量 s_cache_line_size，类型是 static size_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static size_t s_cache_line_size = 0;


/* 这里把 {0} 写入 static、uint8、t、s、cam、缓冲区、相机、NUM、BUFS；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static uint8_t *s_cam_buf[CAMERA_NUM_BUFS] = {0};

/* 这里定义变量 s_cam_buf_size，类型是 static size_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static size_t s_cam_buf_size = 0;


/* 这里把 {0} 写入 static、uint8、t、s、stage、缓冲区、STAGE、NUM、BUFS；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static uint8_t *s_stage_buf[STAGE_NUM_BUFS] = {0};

/* 这里把 NULL 写入 static、uint8、t、s、界面、canvas、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static uint8_t *s_ui_canvas_buf = NULL;

/* 这里定义变量 s_disp_buf_size，类型是 static uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static uint32_t s_disp_buf_size = 0;


/* 这里定义变量 s_display_task_handle，类型是 static TaskHandle_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
static TaskHandle_t s_display_task_handle = NULL;

/* 这里定义变量 s_display_mux，类型是 static portMUX_TYPE，并且在声明时就把初值设成 portMUX_INITIALIZER_UNLOCKED；这样后面第一次使用它时就是一个确定状态。 */
static portMUX_TYPE s_display_mux = portMUX_INITIALIZER_UNLOCKED;



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef enum {

    /* 这里把 0, 写入 DISP、缓冲区、FREE；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    DISP_BUF_FREE = 0,
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    DISP_BUF_WRITING,
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    DISP_BUF_READY,

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} disp_buf_state_t;


/* 这里定义变量 s_pending_stage_index，类型是 volatile int，并且在声明时就把初值设成 -1；这样后面第一次使用它时就是一个确定状态。 */
static volatile int s_pending_stage_index = -1;

/* 这里定义变量 s_vision_sample_skip，类型是 static uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static uint32_t s_vision_sample_skip = 0;

/* 这里定义变量 s_stage_state，类型是 static disp_buf_state_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static disp_buf_state_t s_stage_state[STAGE_NUM_BUFS] = {0};

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if SOC_PPA_SUPPORTED

/* 这里定义变量 s_ppa_srm_handle，类型是 static ppa_client_handle_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
static ppa_client_handle_t s_ppa_srm_handle = NULL;
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif






/* 这里开始定义函数 app_camera_msync_aligned；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_camera_msync_aligned(void *addr, size_t size, int flags)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 addr == NULL || size == 0；只有条件成立，后面的分支代码才会执行。 */
    if (addr == NULL || size == 0) {

        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 align，类型是 size_t，并且在声明时就把初值设成 s_cache_line_size ? s_cache_line_size : 64；这样后面第一次使用它时就是一个确定状态。 */
    size_t align = s_cache_line_size ? s_cache_line_size : 64;

    /* 这里定义变量 start，类型是 uintptr_t，并且在声明时就把初值设成 (uintptr_t)addr & ~((uintptr_t)align - 1U)；这样后面第一次使用它时就是一个确定状态。 */
    uintptr_t start = (uintptr_t)addr & ~((uintptr_t)align - 1U);

    /* 这里定义变量 end，类型是 uintptr_t，并且在声明时就把初值设成 ((uintptr_t)addr + size + align - 1U) & ~((uintptr_t)align - 1U)；这样后面第一次使用它时就是一个确定状态。 */
    uintptr_t end = ((uintptr_t)addr + size + align - 1U) & ~((uintptr_t)align - 1U);


    /* 这里把 esp_cache_msync((void *)start, end - start, flags) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return esp_cache_msync((void *)start, end - start, flags);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_msync_m2c；返回类型是 static inline esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline esp_err_t app_camera_msync_m2c(const void *addr, size_t size)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用本项目模块接口 app_camera_msync_aligned；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    return app_camera_msync_aligned((void *)addr, size,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C |
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    ESP_CACHE_MSYNC_FLAG_INVALIDATE);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_msync_c2m；返回类型是 static inline esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline esp_err_t app_camera_msync_c2m(void *addr, size_t size)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用本项目模块接口 app_camera_msync_aligned；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    return app_camera_msync_aligned(addr, size,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    ESP_CACHE_MSYNC_FLAG_INVALIDATE);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 调用本项目模块接口 app_get_active_screen；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
static lv_obj_t *app_get_active_screen(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if LVGL_VERSION_MAJOR >= 9

    /* 这里把 lv_screen_active() 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return lv_screen_active();
/* 进入条件编译的兜底分支；前面的编译条件都不满足时会走这里。 */
#else

    /* 这里把 lv_scr_act() 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return lv_scr_act();
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_free_camera_buffers；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_free_camera_buffers(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {

        /* 这里开始判断条件 s_cam_buf[i]；只有条件成立，后面的分支代码才会执行。 */
        if (s_cam_buf[i]) {

            /* 释放前面通过能力堆接口申请的内存；避免长时间运行后泄漏。 */
            heap_caps_free(s_cam_buf[i]);

            /* 这里把 NULL 写入 s、cam、缓冲区、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_cam_buf[i] = NULL;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 0 写入 s、cam、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cam_buf_size = 0;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_free_display_buffers；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_free_display_buffers(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {

        /* 这里开始判断条件 s_stage_buf[i]；只有条件成立，后面的分支代码才会执行。 */
        if (s_stage_buf[i]) {

            /* 释放前面通过能力堆接口申请的内存；避免长时间运行后泄漏。 */
            heap_caps_free(s_stage_buf[i]);

            /* 这里把 NULL 写入 s、stage、缓冲区、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_stage_buf[i] = NULL;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里把 DISP_BUF_FREE 写入 s、stage、状态、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_stage_state[i] = DISP_BUF_FREE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 s_ui_canvas_buf；只有条件成立，后面的分支代码才会执行。 */
    if (s_ui_canvas_buf) {

        /* 释放前面通过能力堆接口申请的内存；避免长时间运行后泄漏。 */
        heap_caps_free(s_ui_canvas_buf);

        /* 这里把 NULL 写入 s、界面、canvas、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_ui_canvas_buf = NULL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 -1 写入 s、pending、stage、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_pending_stage_index = -1;

    /* 这里把 0 写入 s、disp、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_disp_buf_size = 0;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_alloc_display_buffers；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_camera_alloc_display_buffers(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_line_size) 写入 s、disp、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_disp_buf_size = ALIGN_UP(BSP_LCD_H_RES * BSP_LCD_V_RES * 2, s_cache_line_size);


    /* 这里把 heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM) 写入 s、界面、canvas、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_ui_canvas_buf = heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM);

    /* 这里开始判断条件 s_ui_canvas_buf == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_ui_canvas_buf == NULL) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "alloc ui canvas buffer failed");

        /* 这里把 ESP_ERR_NO_MEM 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_NO_MEM;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {

        /* 这里把 heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM) 写入 s、stage、缓冲区、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_stage_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, s_disp_buf_size, MALLOC_CAP_SPIRAM);

        /* 这里开始判断条件 s_stage_buf[i] == NULL；只有条件成立，后面的分支代码才会执行。 */
        if (s_stage_buf[i] == NULL) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "alloc stage buffer %d failed", i);

            /* 调用本项目模块接口 app_camera_free_display_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_camera_free_display_buffers();

            /* 这里把 ESP_ERR_NO_MEM 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ESP_ERR_NO_MEM;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里把 DISP_BUF_FREE 写入 s、stage、状态、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_stage_state[i] = DISP_BUF_FREE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_alloc_userptr_buffers；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_camera_alloc_userptr_buffers(size_t frame_size)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_cache_line_size == 0；只有条件成立，后面的分支代码才会执行。 */
    if (s_cache_line_size == 0) {

        /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size)；这样后面第一次使用它时就是一个确定状态。 */
        esp_err_t ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line_size);

        /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
        if (ret != ESP_OK) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "esp_cache_get_alignment failed: %s", esp_err_to_name(ret));

            /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ret;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ALIGN_UP(frame_size, s_cache_line_size) 写入 s、cam、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cam_buf_size = ALIGN_UP(frame_size, s_cache_line_size);

    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < CAMERA_NUM_BUFS; i++) {

        /* 这里把 heap_caps_aligned_calloc(s_cache_line_size, 1, s_cam_buf_size, MALLOC_CAP_SPIRAM) 写入 s、cam、缓冲区、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_cam_buf[i] = heap_caps_aligned_calloc(s_cache_line_size, 1, s_cam_buf_size, MALLOC_CAP_SPIRAM);

        /* 这里开始判断条件 s_cam_buf[i] == NULL；只有条件成立，后面的分支代码才会执行。 */
        if (s_cam_buf[i] == NULL) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "alloc USERPTR camera buffer %d failed", i);

            /* 调用本项目模块接口 app_camera_free_camera_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_camera_free_camera_buffers();

            /* 这里把 ESP_ERR_NO_MEM 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ESP_ERR_NO_MEM;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "USERPTR camera buffers ready: %d x %u bytes", CAMERA_NUM_BUFS, (unsigned)s_cam_buf_size);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_create_canvas；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_camera_create_canvas(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_camera_canvas != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_camera_canvas != NULL) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_get_active_screen() 写入 lv、obj、t、scr；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    lv_obj_t *scr = app_get_active_screen();

    /* 这里把 lv_canvas_create(scr) 写入 s、相机、canvas；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_camera_canvas = lv_canvas_create(scr);

    /* 这里开始判断条件 s_camera_canvas == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_camera_canvas == NULL) {

        /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
        bsp_display_unlock();

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if LVGL_VERSION_MAJOR >= 9

    /* 把用户自己准备的像素缓冲绑定给 LVGL 画布；这样显示和你自己的帧缓冲就能直接连起来。 */
    lv_canvas_set_buffer(s_camera_canvas, s_ui_canvas_buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_COLOR_FORMAT_RGB565);
/* 进入条件编译的兜底分支；前面的编译条件都不满足时会走这里。 */
#else

    /* 把用户自己准备的像素缓冲绑定给 LVGL 画布；这样显示和你自己的帧缓冲就能直接连起来。 */
    lv_canvas_set_buffer(s_camera_canvas, s_ui_canvas_buf, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_IMG_CF_TRUE_COLOR);
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif

    /* 把当前控件放到父容器中心；这能省掉手算坐标。 */
    lv_obj_center(s_camera_canvas);

    /* 把当前控件移到更靠后的层级；常用于让预览画面在文字/HUD 的下面。 */
    lv_obj_move_background(s_camera_canvas);


    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if SOC_PPA_SUPPORTED





/* 这里开始定义函数 app_ppa_init；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ppa_init(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_ppa_srm_handle；只有条件成立，后面的分支代码才会执行。 */
    if (s_ppa_srm_handle) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里定义变量 cfg，类型是 ppa_client_config_t，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
    ppa_client_config_t cfg = {
        /* 这里把 PPA_OPERATION_SRM, 写入 oper、类型；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .oper_type = PPA_OPERATION_SRM,
    /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
    };


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 ppa_register_client(&cfg, &s_ppa_srm_handle)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = ppa_register_client(&cfg, &s_ppa_srm_handle);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "ppa_register_client failed: 0x%x", ret);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 calc_aspect_fit；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void calc_aspect_fit(uint32_t src_w, uint32_t src_h,
                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                            uint32_t dst_w, uint32_t dst_h,
                            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                            uint32_t *out_w, uint32_t *out_h)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里定义变量 src_aspect，类型是 const float，并且在声明时就把初值设成 (float)src_w / (float)src_h；这样后面第一次使用它时就是一个确定状态。 */
    const float src_aspect = (float)src_w / (float)src_h;

    /* 这里定义变量 dst_aspect，类型是 const float，并且在声明时就把初值设成 (float)dst_w / (float)dst_h；这样后面第一次使用它时就是一个确定状态。 */
    const float dst_aspect = (float)dst_w / (float)dst_h;


    /* 这里开始判断条件 src_aspect > dst_aspect；只有条件成立，后面的分支代码才会执行。 */
    if (src_aspect > dst_aspect) {
        /* 这里把 dst_w 写入 out、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *out_w = dst_w;
        /* 这里把 (uint32_t)(dst_w / src_aspect) 写入 out、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *out_h = (uint32_t)(dst_w / src_aspect);
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {
        /* 这里把 dst_h 写入 out、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *out_h = dst_h;
        /* 这里把 (uint32_t)(dst_h * src_aspect) 写入 out、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *out_w = (uint32_t)(dst_h * src_aspect);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_image_process_scale_crop；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_image_process_scale_crop(uint8_t *in_buf,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              uint32_t in_width,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              uint32_t in_height,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              uint8_t *out_buf,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              uint32_t out_width,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              uint32_t out_height,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              size_t out_buf_size,
                                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                              ppa_srm_rotation_angle_t rotation_angle)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里定义变量 scale_x，类型是 float，并且在声明时就把初值设成 (float)out_width / (float)in_width；这样后面第一次使用它时就是一个确定状态。 */
    float scale_x = (float)out_width / (float)in_width;

    /* 这里定义变量 scale_y，类型是 float，并且在声明时就把初值设成 (float)out_height / (float)in_height；这样后面第一次使用它时就是一个确定状态。 */
    float scale_y = (float)out_height / (float)in_height;


    /* 这里开始判断条件 rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || rotation_angle == PPA_SRM_ROTATION_ANGLE_270；只有条件成立，后面的分支代码才会执行。 */
    if (rotation_angle == PPA_SRM_ROTATION_ANGLE_90 || rotation_angle == PPA_SRM_ROTATION_ANGLE_270) {

        /* 这里把 (float)out_height / (float)in_width 写入 scale、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        scale_x = (float)out_height / (float)in_width;

        /* 这里把 (float)out_width / (float)in_height 写入 scale、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        scale_y = (float)out_width / (float)in_height;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(out_buf, 0, out_buf_size);

    /* 这里定义变量 srm_cfg，类型是 ppa_srm_oper_config_t，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
    ppa_srm_oper_config_t srm_cfg = {
        /* 这里把 in_buf, 写入 in、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.buffer = in_buf,
        /* 这里把 in_width, 写入 in、pic、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.pic_w = in_width,
        /* 这里把 in_height, 写入 in、pic、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.pic_h = in_height,
        /* 这里把 in_width, 写入 in、block、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.block_w = in_width,
        /* 这里把 in_height, 写入 in、block、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.block_h = in_height,
        /* 这里把 0, 写入 in、block、offset、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.block_offset_x = 0,
        /* 这里把 0, 写入 in、block、offset、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.block_offset_y = 0,
        /* 这里把 PPA_SRM_COLOR_MODE_RGB565, 写入 in、srm、cm；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        /* 这里把 out_buf, 写入 out、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.buffer = out_buf,
        /* 这里把 out_buf_size, 写入 out、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.buffer_size = out_buf_size,
        /* 这里把 BSP_LCD_H_RES, 写入 out、pic、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.pic_w = BSP_LCD_H_RES,
        /* 这里把 BSP_LCD_V_RES, 写入 out、pic、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.pic_h = BSP_LCD_V_RES,
        /* 这里把 (BSP_LCD_H_RES - out_width) / 2, 写入 out、block、offset、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.block_offset_x = (BSP_LCD_H_RES - out_width) / 2,
        /* 这里把 (BSP_LCD_V_RES - out_height) / 2, 写入 out、block、offset、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.block_offset_y = (BSP_LCD_V_RES - out_height) / 2,
        /* 这里把 PPA_SRM_COLOR_MODE_RGB565, 写入 out、srm、cm；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        /* 这里把 rotation_angle, 写入 rotation、angle；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .rotation_angle = rotation_angle,
        /* 这里把 scale_x, 写入 scale、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .scale_x = scale_x,
        /* 这里把 scale_y, 写入 scale、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .scale_y = scale_y,
        /* 这里把 0, 写入 RGB、swap；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .rgb_swap = 0,
        /* 这里把 0, 写入 byte、swap；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .byte_swap = 0,
        /* 这里把 PPA_TRANS_MODE_BLOCKING, 写入 模式；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .mode = PPA_TRANS_MODE_BLOCKING,
    /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
    };


    /* 这里把 ppa_do_scale_rotate_mirror(s_ppa_srm_handle, &srm_cfg) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ppa_do_scale_rotate_mirror(s_ppa_srm_handle, &srm_cfg);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif






/* 这里开始定义函数 app_camera_has_pending_stage；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_camera_has_pending_stage(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 has_pending，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool has_pending = false;


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_display_mux);

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    has_pending = (s_pending_stage_index >= 0);

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_display_mux);


    /* 这里把 has_pending 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return has_pending;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_pick_writable_stage_buffer；返回类型是 static int，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static int app_camera_pick_writable_stage_buffer(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 idx，类型是 int，并且在声明时就把初值设成 -1；这样后面第一次使用它时就是一个确定状态。 */
    int idx = -1;


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_display_mux);

    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < STAGE_NUM_BUFS; i++) {

        /* 这里开始判断条件 s_stage_state[i] == DISP_BUF_FREE；只有条件成立，后面的分支代码才会执行。 */
        if (s_stage_state[i] == DISP_BUF_FREE) {

            /* 这里把 DISP_BUF_WRITING 写入 s、stage、状态、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_stage_state[i] = DISP_BUF_WRITING;

            /* 这里把 i 写入 索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            idx = i;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_display_mux);


    /* 这里把 idx 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return idx;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_publish_stage_buffer；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_publish_stage_buffer(int ready_index)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_display_mux);

    /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY) {
        /* 这里把 DISP_BUF_FREE 写入 s、stage、状态、s、pending、stage、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_stage_state[s_pending_stage_index] = DISP_BUF_FREE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 DISP_BUF_READY 写入 s、stage、状态、就绪、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_stage_state[ready_index] = DISP_BUF_READY;

    /* 这里把 ready_index 写入 s、pending、stage、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_pending_stage_index = ready_index;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_display_mux);


    /* 这里开始判断条件 s_display_task_handle；只有条件成立，后面的分支代码才会执行。 */
    if (s_display_task_handle) {

        /* 调用 FreeRTOS 任务接口 xTaskNotifyGive；这里通常在创建任务、延时或查询任务运行状态。 */
        xTaskNotifyGive(s_display_task_handle);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_abandon_stage_buffer；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_abandon_stage_buffer(int buf_index)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 buf_index < 0 || buf_index >= STAGE_NUM_BUFS；只有条件成立，后面的分支代码才会执行。 */
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_display_mux);

    /* 这里开始判断条件 s_stage_state[buf_index] == DISP_BUF_WRITING；只有条件成立，后面的分支代码才会执行。 */
    if (s_stage_state[buf_index] == DISP_BUF_WRITING) {

        /* 这里把 DISP_BUF_FREE 写入 s、stage、状态、缓冲区、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_stage_state[buf_index] = DISP_BUF_FREE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_display_mux);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_take_ready_stage_buffer；返回类型是 static int，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static int app_camera_take_ready_stage_buffer(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 idx，类型是 int，并且在声明时就把初值设成 -1；这样后面第一次使用它时就是一个确定状态。 */
    int idx = -1;


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_display_mux);

    /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
    if (s_pending_stage_index >= 0 && s_pending_stage_index < STAGE_NUM_BUFS &&
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        s_stage_state[s_pending_stage_index] == DISP_BUF_READY) {
        /* 这里把 s_pending_stage_index 写入 索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        idx = s_pending_stage_index;

        /* 这里把 -1 写入 s、pending、stage、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_pending_stage_index = -1;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_display_mux);


    /* 这里把 idx 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return idx;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_release_stage_buffer；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_release_stage_buffer(int buf_index)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 buf_index < 0 || buf_index >= STAGE_NUM_BUFS；只有条件成立，后面的分支代码才会执行。 */
    if (buf_index < 0 || buf_index >= STAGE_NUM_BUFS) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_display_mux);

    /* 这里把 DISP_BUF_FREE 写入 s、stage、状态、缓冲区、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_stage_state[buf_index] = DISP_BUF_FREE;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_display_mux);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_display_task；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_display_task(void *arg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)arg;


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (1) {

        /* 调用函数 ulTaskNotifyTake；从名字看，它承担的职责和“ulTaskNotifyTake”有关，后续行为取决于这个接口的返回结果或副作用。 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);


        /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
        while (1) {

            /* 这里定义变量 buf_index，类型是 int，并且在声明时就把初值设成 app_camera_take_ready_stage_buffer()；这样后面第一次使用它时就是一个确定状态。 */
            int buf_index = app_camera_take_ready_stage_buffer();

            /* 这里开始判断条件 buf_index < 0 || s_camera_canvas == NULL || s_ui_canvas_buf == NULL；只有条件成立，后面的分支代码才会执行。 */
            if (buf_index < 0 || s_camera_canvas == NULL || s_ui_canvas_buf == NULL) {

                /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
                break;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里开始判断条件 app_camera_msync_m2c(s_stage_buf[buf_index], s_disp_buf_size) != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
            if (app_camera_msync_m2c(s_stage_buf[buf_index], s_disp_buf_size) != ESP_OK) {

                /* 调用本项目模块接口 app_camera_release_stage_buffer；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_camera_release_stage_buffer(buf_index);

                /* 这里直接结束当前这一轮循环，马上进入下一轮；常用于过滤掉当前不想处理的数据。 */
                continue;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里开始判断条件 bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
            if (bsp_display_lock(0)) {

                /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
                memcpy(s_ui_canvas_buf, s_stage_buf[buf_index], s_disp_buf_size);

                /* 调用本项目模块接口 app_camera_msync_c2m；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_camera_msync_c2m(s_ui_canvas_buf, s_disp_buf_size);

                /* 标记控件需要重新绘制；新图像或文字改了之后通常会触发这一步。 */
                lv_obj_invalidate(s_camera_canvas);

                /* 让 LVGL 立即执行一次刷新；当你想尽快把结果刷上屏幕时会用到。 */
                lv_refr_now(NULL);

                /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
                bsp_display_unlock();
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 调用本项目模块接口 app_camera_release_stage_buffer；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_camera_release_stage_buffer(buf_index);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_camera_frame_cb；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_camera_frame_cb(uint8_t *camera_buf,
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                uint8_t camera_buf_index,
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                uint32_t camera_buf_hes,
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                uint32_t camera_buf_ves,
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                size_t camera_buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)camera_buf_index;


    /* 这里开始判断条件 camera_buf == NULL || s_camera_canvas == NULL || s_ui_canvas_buf == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (camera_buf == NULL || s_camera_canvas == NULL || s_ui_canvas_buf == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (app_camera_msync_m2c(camera_buf, camera_buf_len) != ESP_OK) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 ++s_vision_sample_skip >= VISION_SAMPLE_INTERVAL；只有条件成立，后面的分支代码才会执行。 */
    if (++s_vision_sample_skip >= VISION_SAMPLE_INTERVAL) {

        /* 这里把 0 写入 s、视觉、sample、skip；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_vision_sample_skip = 0;

        /* 把当前抽样帧送给视觉线程；这样显示和识别可以解耦并行。 */
        (void)app_vision_submit_frame(camera_buf,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      camera_buf_hes,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      camera_buf_ves,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      camera_buf_len);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }








    /* 这里开始判断条件 app_camera_has_pending_stage()；只有条件成立，后面的分支代码才会执行。 */
    if (app_camera_has_pending_stage()) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 stage_index，类型是 int，并且在声明时就把初值设成 app_camera_pick_writable_stage_buffer()；这样后面第一次使用它时就是一个确定状态。 */
    int stage_index = app_camera_pick_writable_stage_buffer();

    /* 这里开始判断条件 stage_index < 0；只有条件成立，后面的分支代码才会执行。 */
    if (stage_index < 0) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 out_w，类型是 uint32_t，并且在声明时就把初值设成 BSP_LCD_H_RES；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t out_w = BSP_LCD_H_RES;

    /* 这里定义变量 out_h，类型是 uint32_t，并且在声明时就把初值设成 BSP_LCD_V_RES；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t out_h = BSP_LCD_V_RES;

    /* 这里把 s_stage_buf[stage_index] 写入 uint8、t、out、缓冲区；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    uint8_t *out_buf = s_stage_buf[stage_index];

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if SOC_PPA_SUPPORTED

    /* 这里开始判断条件 s_ppa_srm_handle；只有条件成立，后面的分支代码才会执行。 */
    if (s_ppa_srm_handle) {

        /* 这里定义变量 rotation，类型是 ppa_srm_rotation_angle_t，并且在声明时就把初值设成 PPA_SRM_ROTATION_ANGLE_0；这样后面第一次使用它时就是一个确定状态。 */
        ppa_srm_rotation_angle_t rotation = PPA_SRM_ROTATION_ANGLE_0;

        /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
        switch (BSP_CAMERA_ROTATION) {

            /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
            case 90:  rotation = PPA_SRM_ROTATION_ANGLE_90; break;

            /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
            case 180: rotation = PPA_SRM_ROTATION_ANGLE_180; break;

            /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
            case 270: rotation = PPA_SRM_ROTATION_ANGLE_270; break;

            /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
            case 0:

            /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
            default:  rotation = PPA_SRM_ROTATION_ANGLE_0; break;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 BSP_CAMERA_ROTATION == 90 || BSP_CAMERA_ROTATION == 270；只有条件成立，后面的分支代码才会执行。 */
        if (BSP_CAMERA_ROTATION == 90 || BSP_CAMERA_ROTATION == 270) {

            /* 调用函数 calc_aspect_fit；从名字看，它承担的职责和“calc、aspect、fit”有关，后续行为取决于这个接口的返回结果或副作用。 */
            calc_aspect_fit(camera_buf_ves, camera_buf_hes, BSP_LCD_H_RES, BSP_LCD_V_RES, &out_w, &out_h);
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 调用函数 calc_aspect_fit；从名字看，它承担的职责和“calc、aspect、fit”有关，后续行为取决于这个接口的返回结果或副作用。 */
            calc_aspect_fit(camera_buf_hes, camera_buf_ves, BSP_LCD_H_RES, BSP_LCD_V_RES, &out_w, &out_h);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
        if (app_image_process_scale_crop(camera_buf,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         camera_buf_hes,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         camera_buf_ves,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         out_buf,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         out_w,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         out_h,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         s_disp_buf_size,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         rotation) != ESP_OK) {
            /* 调用本项目模块接口 app_camera_abandon_stage_buffer；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_camera_abandon_stage_buffer(stage_index);

            /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
            return;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    } else
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
    /* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
    {

        /* 这里定义变量 copy_len，类型是 size_t，并且在声明时就把初值设成 camera_buf_len < s_disp_buf_size ? camera_buf_len : s_disp_buf_size；这样后面第一次使用它时就是一个确定状态。 */
        size_t copy_len = camera_buf_len < s_disp_buf_size ? camera_buf_len : s_disp_buf_size;

        /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
        memcpy(out_buf, camera_buf, copy_len);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 app_camera_msync_m2c(out_buf, s_disp_buf_size) != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (app_camera_msync_m2c(out_buf, s_disp_buf_size) != ESP_OK) {

        /* 调用本项目模块接口 app_camera_abandon_stage_buffer；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_camera_abandon_stage_buffer(stage_index);

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_camera_publish_stage_buffer；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_camera_publish_stage_buffer(stage_index);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_start_display_task；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_camera_start_display_task(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_display_task_handle；只有条件成立，后面的分支代码才会执行。 */
    if (s_display_task_handle) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 ret，类型是 BaseType_t，并且在声明时就把初值设成 xTaskCreatePinnedToCore(app_camera_display_task,；这样后面第一次使用它时就是一个确定状态。 */
    BaseType_t ret = xTaskCreatePinnedToCore(app_camera_display_task,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             "cam_display",
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             DISPLAY_TASK_STACK_SIZE,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             NULL,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             DISPLAY_TASK_PRIORITY,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             &s_display_task_handle,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             1);

    /* 这里把 (ret == pdPASS) ? ESP_OK : ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_init；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_camera_init(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_camera_inited；只有条件成立，后面的分支代码才会执行。 */
    if (s_camera_inited) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if !BSP_CAPS_CAMERA

    /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
    ESP_LOGE(TAG, "this BSP does not support camera");

    /* 这里把 ESP_ERR_NOT_SUPPORTED 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_ERR_NOT_SUPPORTED;
/* 进入条件编译的兜底分支；前面的编译条件都不满足时会走这里。 */
#else

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "starting camera preview pipeline (USERPTR + stage queue + fixed canvas)");


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 bsp_camera_start(NULL)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = bsp_camera_start(NULL);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "bsp_camera_start failed: %s", esp_err_to_name(ret));

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if SOC_PPA_SUPPORTED

    /* 调用本项目模块接口 app_ppa_init；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ppa_init();
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif


    /* 这里把 app_camera_alloc_display_buffers() 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = app_camera_alloc_display_buffers();

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_camera_create_canvas() 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = app_camera_create_canvas();

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 调用本项目模块接口 app_camera_free_display_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_camera_free_display_buffers();

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_camera_start_display_task() 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = app_camera_start_display_task();

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 调用本项目模块接口 app_camera_free_display_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_camera_free_display_buffers();

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_video_register_frame_operation_cb(app_camera_frame_cb) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = app_video_register_frame_operation_cb(app_camera_frame_cb);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 调用本项目模块接口 app_camera_free_display_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_camera_free_display_buffers();

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 true 写入 s、相机、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_camera_inited = true;

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_preview_start；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_camera_preview_start(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_camera_inited；只有条件成立，后面的分支代码才会执行。 */
    if (!s_camera_inited) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 s_preview_running；只有条件成立，后面的分支代码才会执行。 */
    if (s_preview_running) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_video_open(BSP_CAMERA_DEVICE, APP_VIDEO_FMT_RGB565) 写入 s、视频、fd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video_fd = app_video_open(BSP_CAMERA_DEVICE, APP_VIDEO_FMT_RGB565);

    /* 这里开始判断条件 s_video_fd < 0；只有条件成立，后面的分支代码才会执行。 */
    if (s_video_fd < 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "app_video_open failed");

        /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
        ESP_LOGW(TAG, "please check camera sensor selection in menuconfig");

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_camera_alloc_userptr_buffers(app_video_get_buf_size())；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = app_camera_alloc_userptr_buffers(app_video_get_buf_size());

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 调用函数 close；从名字看，它承担的职责和“关闭”有关，后续行为取决于这个接口的返回结果或副作用。 */
        close(s_video_fd);

        /* 这里把 -1 写入 s、视频、fd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video_fd = -1;

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_video_set_bufs(s_video_fd, CAMERA_NUM_BUFS, (const void **)s_cam_buf) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = app_video_set_bufs(s_video_fd, CAMERA_NUM_BUFS, (const void **)s_cam_buf);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "app_video_set_bufs(USERPTR) failed: %s", esp_err_to_name(ret));

        /* 调用本项目模块接口 app_camera_free_camera_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_camera_free_camera_buffers();

        /* 调用函数 close；从名字看，它承担的职责和“关闭”有关，后续行为取决于这个接口的返回结果或副作用。 */
        close(s_video_fd);

        /* 这里把 -1 写入 s、视频、fd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video_fd = -1;

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_video_stream_task_start(s_video_fd, 0) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = app_video_stream_task_start(s_video_fd, 0);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "app_video_stream_task_start failed: %s", esp_err_to_name(ret));

        /* 调用本项目模块接口 app_camera_free_camera_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_camera_free_camera_buffers();

        /* 调用函数 close；从名字看，它承担的职责和“关闭”有关，后续行为取决于这个接口的返回结果或副作用。 */
        close(s_video_fd);

        /* 这里把 -1 写入 s、视频、fd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video_fd = -1;

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 true 写入 s、预览、running；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_preview_running = true;

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "camera preview started");

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_preview_stop；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_camera_preview_stop(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_preview_running；只有条件成立，后面的分支代码才会执行。 */
    if (!s_preview_running) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_video_stream_task_stop(s_video_fd)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = app_video_stream_task_stop(s_video_fd);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
        ESP_LOGW(TAG, "app_video_stream_task_stop failed: %s", esp_err_to_name(ret));
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_video_stream_wait_stop；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_video_stream_wait_stop();

    /* 调用函数 close；从名字看，它承担的职责和“关闭”有关，后续行为取决于这个接口的返回结果或副作用。 */
    close(s_video_fd);

    /* 这里把 -1 写入 s、视频、fd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video_fd = -1;

    /* 这里把 false 写入 s、预览、running；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_preview_running = false;

    /* 调用本项目模块接口 app_camera_free_camera_buffers；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_camera_free_camera_buffers();

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_camera_is_preview_running；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_camera_is_preview_running(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 s_preview_running 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return s_preview_running;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
