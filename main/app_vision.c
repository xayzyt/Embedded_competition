/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */







/* 引入本项目的 app_vision 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_vision.h"

/* 引入 stdio.h；标准输入输出头文件；常见的 printf、snprintf 等格式化输出接口都在这里声明。 */
#include <stdio.h>
/* 引入 string.h；标准字符串/内存处理头文件；memcpy、memset、strcmp 等基础接口都来自这里。 */
#include <string.h>

/* 引入 freertos/FreeRTOS.h；FreeRTOS 核心头文件；任务、队列、事件组等内核对象的基础定义都依赖它。 */
#include "freertos/FreeRTOS.h"
/* 引入 freertos/task.h；FreeRTOS 任务头文件；xTaskCreate、vTaskDelay、任务通知等接口主要在这里声明。 */
#include "freertos/task.h"

/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"
/* 引入 esp_timer.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_timer.h"

/* 引入本项目的 app_apriltag 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_apriltag.h"
/* 引入本项目的 app_ui 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ui.h"































































/* 定义宏 VISION_TASK_STACK_SIZE；这里把“视觉、任务、STACK、大小”集中写成常量 (16 * 1024)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_TASK_STACK_SIZE         (16 * 1024)
/* 定义宏 VISION_TASK_PRIORITY；这里把“视觉、任务、PRIORITY”集中写成常量 4，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_TASK_PRIORITY           4
/* 定义宏 VISION_TASK_CORE_ID；这里把“视觉、任务、核心、ID”集中写成常量 1，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_TASK_CORE_ID            1
/* 定义宏 VISION_POLL_PERIOD_MS；这里把“视觉、POLL、PERIOD、毫秒”集中写成常量 25，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_POLL_PERIOD_MS          25
/* 定义宏 VISION_HEARTBEAT_MS；这里把“视觉、HEARTBEAT、毫秒”集中写成常量 1000，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_HEARTBEAT_MS            1000
/* 定义宏 VISION_GRAY_WIDTH；这里把“视觉、GRAY、WIDTH”集中写成常量 240，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_GRAY_WIDTH              240
/* 定义宏 VISION_GRAY_HEIGHT；这里把“视觉、GRAY、HEIGHT”集中写成常量 180，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_GRAY_HEIGHT             180
/* 定义宏 VISION_GRAY_BUF_SIZE；这里把“视觉、GRAY、缓冲区、大小”集中写成常量 (VISION_GRAY_WIDTH * VISION_GRAY_HEIGHT)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_GRAY_BUF_SIZE           (VISION_GRAY_WIDTH * VISION_GRAY_HEIGHT)
/* 定义宏 VISION_LOST_RESET_FRAMES；这里把“视觉、LOST、RESET、FRAMES”集中写成常量 2U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_LOST_RESET_FRAMES       2U
/* 定义宏 VISION_STABLE_DECAY_ON_LOST；这里把“视觉、STABLE、DECAY、ON、LOST”集中写成常量 1U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VISION_STABLE_DECAY_ON_LOST    1U


/* 这里把 "app_vision" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_vision";



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这里先定义变量 info，类型是 app_vision_gray_frame_info_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_vision_gray_frame_info_t info;

    /* 这里先定义变量 gray，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t gray[VISION_GRAY_BUF_SIZE];

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_vision_gray_slot_t;


/* 这里定义变量 s_vision_task，类型是 static TaskHandle_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
static TaskHandle_t s_vision_task = NULL;

/* 这里定义变量 s_vision_inited，类型是 static bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
static bool s_vision_inited = false;

/* 这里定义变量 s_vision_mux，类型是 static portMUX_TYPE，并且在声明时就把初值设成 portMUX_INITIALIZER_UNLOCKED；这样后面第一次使用它时就是一个确定状态。 */
static portMUX_TYPE s_vision_mux = portMUX_INITIALIZER_UNLOCKED;


/* 这里定义变量 s_latest_frame，类型是 static app_vision_frame_info_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_vision_frame_info_t s_latest_frame = {0};

/* 这里定义变量 s_gray_slot，类型是 static app_vision_gray_slot_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_vision_gray_slot_t s_gray_slot = {0};

/* 这里定义变量 s_task_slot，类型是 static app_vision_gray_slot_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_vision_gray_slot_t s_task_slot = {0};

/* 这里定义变量 s_submit_slot，类型是 static app_vision_gray_slot_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_vision_gray_slot_t s_submit_slot = {0};

/* 这里定义变量 s_latest_result，类型是 static app_vision_result_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_vision_result_t s_latest_result = {0};

/* 这里定义变量 s_submit_seq，类型是 static uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static uint32_t s_submit_seq = 0;

/* 这里定义变量 s_submit_overwrite，类型是 static uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static uint32_t s_submit_overwrite = 0;

/* 这里定义变量 s_first_submit_logged，类型是 static bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
static bool s_first_submit_logged = false;






/* 这里开始定义函数 app_vision_now_ms；返回类型是 static inline uint32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline uint32_t app_vision_now_ms(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_rgb565_to_gray；返回类型是 static inline uint8_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline uint8_t app_rgb565_to_gray(uint16_t pixel)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 r，类型是 uint32_t，并且在声明时就把初值设成 (pixel >> 11) & 0x1F；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t r = (pixel >> 11) & 0x1F;

    /* 这里定义变量 g，类型是 uint32_t，并且在声明时就把初值设成 (pixel >> 5) & 0x3F；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t g = (pixel >> 5) & 0x3F;

    /* 这里定义变量 b，类型是 uint32_t，并且在声明时就把初值设成 pixel & 0x1F；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t b = pixel & 0x1F;


    /* 这里把 (r * 255U) / 31U 写入 r；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    r = (r * 255U) / 31U;

    /* 这里把 (g * 255U) / 63U 写入 g；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    g = (g * 255U) / 63U;

    /* 这里把 (b * 255U) / 31U 写入 b；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    b = (b * 255U) / 31U;


    /* 这里把 (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_snapshot；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_vision_snapshot(app_vision_frame_info_t *meta_out,
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                app_vision_gray_slot_t *slot_out,
                                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                uint32_t *overwrite_out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_vision_mux);
    /* 这里把 s_latest_frame 写入 meta、out；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *meta_out = s_latest_frame;

    /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
    memcpy(slot_out, &s_gray_slot, sizeof(app_vision_gray_slot_t));
    /* 这里把 s_submit_overwrite 写入 overwrite、out；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *overwrite_out = s_submit_overwrite;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_vision_mux);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_store_result；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_vision_store_result(const app_vision_result_t *result)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_vision_mux);

    /* 这里把 *result 写入 s、latest、结果；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_latest_result = *result;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_vision_mux);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_get_latest_result；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_vision_get_latest_result(app_vision_result_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (out == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_vision_mux);
    /* 这里把 s_latest_result 写入 out；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *out = s_latest_result;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_vision_mux);


    /* 这里把 out->valid 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return out->valid;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_set_wait_text；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_vision_set_wait_text(uint32_t heartbeat)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里先定义变量 buf，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char buf[64];

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(buf, sizeof(buf), "tag: wait #%lu", (unsigned long)heartbeat);

    /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_vision_text(buf);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_vision_update_result；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_vision_update_result(const app_vision_gray_slot_t *slot,
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     const app_apriltag_result_t *tag,
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     uint32_t detect_ms,
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     uint16_t *stable_count,
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     uint16_t *lost_count,
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     uint16_t *last_tag_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里定义变量 result，类型是 app_vision_result_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    app_vision_result_t result = {0};


    /* 这里开始判断条件 tag != NULL && tag->valid；只有条件成立，后面的分支代码才会执行。 */
    if (tag != NULL && tag->valid) {

        /* 这里开始判断条件 *last_tag_id == tag->id；只有条件成立，后面的分支代码才会执行。 */
        if (*last_tag_id == tag->id) {

            /* 这里开始判断条件 *stable_count < UINT16_MAX；只有条件成立，后面的分支代码才会执行。 */
            if (*stable_count < UINT16_MAX) {

                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                (*stable_count)++;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {
            /* 这里把 1 写入 stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *stable_count = 1;
            /* 这里把 tag->id 写入 last、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *last_tag_id = tag->id;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里把 0 写入 lost、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *lost_count = 0;


        /* 这里把 true 写入 结果、valid；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.valid = true;

        /* 这里把 tag->id 写入 结果、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.tag_id = tag->id;

        /* 这里把 tag->hamming 写入 结果、hamming；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.hamming = tag->hamming;

        /* 这里把 tag->rotation 写入 结果、rotation；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.rotation = tag->rotation;

        /* 这里把 tag->threshold 写入 结果、threshold；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.threshold = tag->threshold;

        /* 这里把 tag->border_dark_pct 写入 结果、border、dark、pct；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.border_dark_pct = tag->border_dark_pct;

        /* 这里把 tag->center_x 写入 结果、center、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.center_x = tag->center_x;

        /* 这里把 tag->center_y 写入 结果、center、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.center_y = tag->center_y;

        /* 这里把 tag->area 写入 结果、area；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.area = tag->area;

        /* 这里把 tag->bbox_x 写入 结果、bbox、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.bbox_x = tag->bbox_x;

        /* 这里把 tag->bbox_y 写入 结果、bbox、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.bbox_y = tag->bbox_y;

        /* 这里把 tag->bbox_w 写入 结果、bbox、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.bbox_w = tag->bbox_w;

        /* 这里把 tag->bbox_h 写入 结果、bbox、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.bbox_h = tag->bbox_h;

        /* 这里把 tag->corner_tl_x 写入 结果、corner、tl、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_tl_x = tag->corner_tl_x;

        /* 这里把 tag->corner_tl_y 写入 结果、corner、tl、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_tl_y = tag->corner_tl_y;

        /* 这里把 tag->corner_tr_x 写入 结果、corner、tr、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_tr_x = tag->corner_tr_x;

        /* 这里把 tag->corner_tr_y 写入 结果、corner、tr、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_tr_y = tag->corner_tr_y;

        /* 这里把 tag->corner_br_x 写入 结果、corner、br、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_br_x = tag->corner_br_x;

        /* 这里把 tag->corner_br_y 写入 结果、corner、br、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_br_y = tag->corner_br_y;

        /* 这里把 tag->corner_bl_x 写入 结果、corner、bl、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_bl_x = tag->corner_bl_x;

        /* 这里把 tag->corner_bl_y 写入 结果、corner、bl、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.corner_bl_y = tag->corner_bl_y;

        /* 这里把 tag->edge_px_avg 写入 结果、edge、像素、avg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.edge_px_avg = tag->edge_px_avg;

        /* 这里把 tag->top_edge_angle_deg 写入 结果、top、edge、angle、deg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.top_edge_angle_deg = tag->top_edge_angle_deg;

        /* 这里把 slot->info.seq 写入 结果、帧、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.frame_seq = slot->info.seq;

        /* 这里把 detect_ms 写入 结果、detect、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.detect_ms = detect_ms;

        /* 这里把 *stable_count 写入 结果、stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.stable_count = *stable_count;

        /* 这里把 *lost_count 写入 结果、lost、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        result.lost_count = *lost_count;


        /* 调用本项目模块接口 app_vision_store_result；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_vision_store_result(&result);


        /* 这里先定义变量 buf，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
        char buf[128];

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 sizeof(buf),
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 "id:%u hm:%u st:%u e:%.1f ang:%d",
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.tag_id,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.hamming,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.stable_count,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (double)result.edge_px_avg,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (int)result.top_edge_angle_deg);

        /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_set_vision_text(buf);


        /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
        ESP_LOGI(TAG,
                 /* 这里把 %lu id=%u hm=%u rot=%u th=%u border=%u area=%ld center=(%ld,%ld) bbox=(%ld,%ld,%ld,%ld) edge=%.1f ang=%.1f stable=%u detect=%lums", 写入 标签、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                 "tag seq=%lu id=%u hm=%u rot=%u th=%u border=%u area=%ld center=(%ld,%ld) bbox=(%ld,%ld,%ld,%ld) edge=%.1f ang=%.1f stable=%u detect=%lums",
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned long)result.frame_seq,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.tag_id,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.hamming,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.rotation,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.threshold,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.border_dark_pct,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.area,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.center_x,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.center_y,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.bbox_x,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.bbox_y,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.bbox_w,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (long)result.bbox_h,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (double)result.edge_px_avg,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (double)result.top_edge_angle_deg,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned)result.stable_count,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned long)result.detect_ms);


        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 *lost_count < UINT16_MAX；只有条件成立，后面的分支代码才会执行。 */
    if (*lost_count < UINT16_MAX) {

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        (*lost_count)++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 (*lost_count < VISION_LOST_RESET_FRAMES) && (*stable_count > VISION_STABLE_DECAY_ON_LOST)；只有条件成立，后面的分支代码才会执行。 */
    if ((*lost_count < VISION_LOST_RESET_FRAMES) && (*stable_count > VISION_STABLE_DECAY_ON_LOST)) {
        /* 这里把 (uint16_t)(*stable_count - VISION_STABLE_DECAY_ON_LOST) 写入 stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *stable_count = (uint16_t)(*stable_count - VISION_STABLE_DECAY_ON_LOST);
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    } else if (*lost_count >= VISION_LOST_RESET_FRAMES) {
        /* 这里把 0 写入 stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *stable_count = 0;
        /* 这里把 0 写入 last、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *last_tag_id = 0;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 false 写入 结果、valid；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    result.valid = false;

    /* 这里把 slot->info.seq 写入 结果、帧、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    result.frame_seq = slot->info.seq;

    /* 这里把 detect_ms 写入 结果、detect、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    result.detect_ms = detect_ms;

    /* 这里把 *stable_count 写入 结果、stable、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    result.stable_count = *stable_count;

    /* 这里把 *lost_count 写入 结果、lost、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    result.lost_count = *lost_count;

    /* 调用本项目模块接口 app_vision_store_result；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_vision_store_result(&result);


    /* 这里先定义变量 buf，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char buf[96];

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(buf,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             sizeof(buf),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "tag: lost #%u st:%u t:%lums",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)*lost_count,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)*stable_count,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned long)detect_ms);

    /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_vision_text(buf);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_task；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_vision_task(void *arg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)arg;


    /* 这里定义变量 heartbeat，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t heartbeat = 0;

    /* 这里定义变量 last_seq，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t last_seq = 0;

    /* 这里定义变量 last_overwrite，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t last_overwrite = 0;

    /* 这里定义变量 last_heartbeat_tick，类型是 TickType_t，并且在声明时就把初值设成 xTaskGetTickCount()；这样后面第一次使用它时就是一个确定状态。 */
    TickType_t last_heartbeat_tick = xTaskGetTickCount();

    /* 这里定义变量 stable_count，类型是 uint16_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint16_t stable_count = 0;

    /* 这里定义变量 lost_count，类型是 uint16_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint16_t lost_count = 0;

    /* 这里定义变量 last_tag_id，类型是 uint16_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint16_t last_tag_id = 0;


    /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_vision_text("tag: wait frame");


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (1) {

        /* 这里先定义变量 meta，类型是 app_vision_frame_info_t；后面真正给它赋值或填内容的代码会继续跟上。 */
        app_vision_frame_info_t meta;

        /* 这里定义变量 overwrite，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint32_t overwrite = 0;

        /* 调用本项目模块接口 app_vision_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_vision_snapshot(&meta, &s_task_slot, &overwrite);


        /* 这里开始判断条件 s_task_slot.info.seq != 0 && s_task_slot.info.seq != last_seq；只有条件成立，后面的分支代码才会执行。 */
        if (s_task_slot.info.seq != 0 && s_task_slot.info.seq != last_seq) {

            /* 这里定义变量 start_us，类型是 int64_t，并且在声明时就把初值设成 esp_timer_get_time()；这样后面第一次使用它时就是一个确定状态。 */
            int64_t start_us = esp_timer_get_time();

            /* 这里定义变量 tag，类型是 app_apriltag_result_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
            app_apriltag_result_t tag = {0};

            /* 这里定义变量 found，类型是 bool，并且在声明时就把初值设成 app_apriltag_detect_tag36h11(s_task_slot.gray,；这样后面第一次使用它时就是一个确定状态。 */
            bool found = app_apriltag_detect_tag36h11(s_task_slot.gray,
                                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                      s_task_slot.info.gray_width,
                                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                      s_task_slot.info.gray_height,
                                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                      &tag);

            /* 这里定义变量 detect_ms，类型是 uint32_t，并且在声明时就把初值设成 (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL)；这样后面第一次使用它时就是一个确定状态。 */
            uint32_t detect_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL);


            /* 这里开始判断条件 found；只有条件成立，后面的分支代码才会执行。 */
            if (found) {

                /* 调用本项目模块接口 app_vision_update_result；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_vision_update_result(&s_task_slot,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &tag,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         detect_ms,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &stable_count,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &lost_count,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &last_tag_id);
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 调用本项目模块接口 app_vision_update_result；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_vision_update_result(&s_task_slot,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         NULL,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         detect_ms,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &stable_count,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &lost_count,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         &last_tag_id);
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里开始判断条件 last_seq != 0 && s_task_slot.info.seq > (last_seq + 1)；只有条件成立，后面的分支代码才会执行。 */
            if (last_seq != 0 && s_task_slot.info.seq > (last_seq + 1)) {

                /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
                ESP_LOGW(TAG,
                         /* 这里把 %lu now=%lu lost=%lu overwrite=%lu", 写入 视觉、帧、jump、last；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                         "vision frame jump: last=%lu now=%lu lost=%lu overwrite=%lu",
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (unsigned long)last_seq,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (unsigned long)s_task_slot.info.seq,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (unsigned long)(s_task_slot.info.seq - last_seq - 1),
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (unsigned long)(overwrite - last_overwrite));
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里把 s_task_slot.info.seq 写入 last、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            last_seq = s_task_slot.info.seq;

            /* 这里把 overwrite 写入 last、overwrite；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            last_overwrite = overwrite;

            /* 这里把 xTaskGetTickCount() 写入 last、heartbeat、节拍；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            last_heartbeat_tick = xTaskGetTickCount();
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        } else if (meta.seq != 0 && meta.seq != last_seq) {

            /* 这里先定义变量 buf，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
            char buf[64];

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     sizeof(buf),
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     "frame#%lu %lux%lu",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned long)meta.seq,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned long)meta.width,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned long)meta.height);

            /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ui_set_vision_text(buf);

            /* 这里把 meta.seq 写入 last、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            last_seq = meta.seq;

            /* 这里把 xTaskGetTickCount() 写入 last、heartbeat、节拍；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            last_heartbeat_tick = xTaskGetTickCount();
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 这里定义变量 now，类型是 TickType_t，并且在声明时就把初值设成 xTaskGetTickCount()；这样后面第一次使用它时就是一个确定状态。 */
            TickType_t now = xTaskGetTickCount();

            /* 这里开始判断条件 (now - last_heartbeat_tick) >= pdMS_TO_TICKS(VISION_HEARTBEAT_MS)；只有条件成立，后面的分支代码才会执行。 */
            if ((now - last_heartbeat_tick) >= pdMS_TO_TICKS(VISION_HEARTBEAT_MS)) {

                /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                heartbeat++;

                /* 调用本项目模块接口 app_vision_set_wait_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_vision_set_wait_text(heartbeat);

                /* 这里把 now 写入 last、heartbeat、节拍；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                last_heartbeat_tick = now;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 让当前 FreeRTOS 任务主动让出 CPU 一段时间；这样不会像 while 死等那样把系统卡住。 */
        vTaskDelay(pdMS_TO_TICKS(VISION_POLL_PERIOD_MS));
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_init；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_vision_init(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_vision_inited；只有条件成立，后面的分支代码才会执行。 */
    if (s_vision_inited) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_apriltag_init()；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = app_apriltag_init();

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "app_apriltag_init failed: %s", esp_err_to_name(ret));

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_vision_mux);

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_latest_frame, 0, sizeof(s_latest_frame));

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_gray_slot, 0, sizeof(s_gray_slot));

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_task_slot, 0, sizeof(s_task_slot));

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_submit_slot, 0, sizeof(s_submit_slot));

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_latest_result, 0, sizeof(s_latest_result));

    /* 这里把 0 写入 s、提交、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_seq = 0;

    /* 这里把 0 写入 s、提交、overwrite；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_overwrite = 0;

    /* 这里把 false 写入 s、first、提交、logged；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_first_submit_logged = false;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_vision_mux);


    /* 这里把 %dx%d", VISION_GRAY_WIDTH, VISION_GRAY_HEIGHT) 写入 ESP、LOGI、标签、视觉、初始化、done、gray；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "vision init done, gray=%dx%d", VISION_GRAY_WIDTH, VISION_GRAY_HEIGHT);

    /* 这里把 true 写入 s、视觉、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_vision_inited = true;

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_vision_start；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_vision_start(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_vision_inited；只有条件成立，后面的分支代码才会执行。 */
    if (!s_vision_inited) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_vision_task != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_vision_task != NULL) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 ret，类型是 BaseType_t，并且在声明时就把初值设成 xTaskCreatePinnedToCore(app_vision_task,；这样后面第一次使用它时就是一个确定状态。 */
    BaseType_t ret = xTaskCreatePinnedToCore(app_vision_task,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             "app_vision",
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             VISION_TASK_STACK_SIZE,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             NULL,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             VISION_TASK_PRIORITY,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             &s_vision_task,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             VISION_TASK_CORE_ID);


    /* 这里开始判断条件 ret != pdPASS；只有条件成立，后面的分支代码才会执行。 */
    if (ret != pdPASS) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "create vision task failed");

        /* 这里把 NULL 写入 s、视觉、任务；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_vision_task = NULL;

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "vision task started");

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_vision_submit_frame；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_vision_submit_frame(const uint8_t *rgb565,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  uint32_t width,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  uint32_t height,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  size_t len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 rgb565 == NULL || width == 0 || height == 0；只有条件成立，后面的分支代码才会执行。 */
    if (rgb565 == NULL || width == 0 || height == 0) {
        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 min_len，类型是 const size_t，并且在声明时就把初值设成 (size_t)width * (size_t)height * 2U；这样后面第一次使用它时就是一个确定状态。 */
    const size_t min_len = (size_t)width * (size_t)height * 2U;

    /* 这里开始判断条件 len < min_len；只有条件成立，后面的分支代码才会执行。 */
    if (len < min_len) {

        /* 这里把 ESP_ERR_INVALID_SIZE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_SIZE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_submit_slot, 0, sizeof(s_submit_slot));


    /* 这里把 width 写入 s、提交、slot、info、源、width；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.src_width = width;

    /* 这里把 height 写入 s、提交、slot、info、源、height；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.src_height = height;

    /* 这里把 VISION_GRAY_WIDTH 写入 s、提交、slot、info、gray、width；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.gray_width = VISION_GRAY_WIDTH;

    /* 这里把 VISION_GRAY_HEIGHT 写入 s、提交、slot、info、gray、height；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.gray_height = VISION_GRAY_HEIGHT;

    /* 这里把 VISION_GRAY_BUF_SIZE 写入 s、提交、slot、info、gray、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.gray_len = VISION_GRAY_BUF_SIZE;


    /* 这里把 (const uint16_t *)rgb565 写入 const、uint16、t、源；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    const uint16_t *src = (const uint16_t *)rgb565;


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (uint32_t gy = 0; gy < VISION_GRAY_HEIGHT; gy++) {

        /* 这里定义变量 sy，类型是 uint32_t，并且在声明时就把初值设成 (gy * height) / VISION_GRAY_HEIGHT；这样后面第一次使用它时就是一个确定状态。 */
        uint32_t sy = (gy * height) / VISION_GRAY_HEIGHT;

        /* 这里开始判断条件 sy >= height；只有条件成立，后面的分支代码才会执行。 */
        if (sy >= height) {

            /* 这里把 height - 1 写入 sy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            sy = height - 1;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
        for (uint32_t gx = 0; gx < VISION_GRAY_WIDTH; gx++) {

            /* 这里定义变量 sx，类型是 uint32_t，并且在声明时就把初值设成 (gx * width) / VISION_GRAY_WIDTH；这样后面第一次使用它时就是一个确定状态。 */
            uint32_t sx = (gx * width) / VISION_GRAY_WIDTH;

            /* 这里开始判断条件 sx >= width；只有条件成立，后面的分支代码才会执行。 */
            if (sx >= width) {

                /* 这里把 width - 1 写入 sx；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                sx = width - 1;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里定义变量 pixel，类型是 uint16_t，并且在声明时就把初值设成 src[sy * width + sx]；这样后面第一次使用它时就是一个确定状态。 */
            uint16_t pixel = src[sy * width + sx];

            /* 这里把 app_rgb565_to_gray(pixel) 写入 s、提交、slot、gray、gy、视觉、GRAY、WIDTH、gx；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_submit_slot.gray[gy * VISION_GRAY_WIDTH + gx] = app_rgb565_to_gray(pixel);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_vision_mux);

    /* 这里开始判断条件 s_gray_slot.info.seq != 0；只有条件成立，后面的分支代码才会执行。 */
    if (s_gray_slot.info.seq != 0) {

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        s_submit_overwrite++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    s_submit_seq++;


    /* 这里把 width 写入 s、latest、帧、width；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_latest_frame.width = width;

    /* 这里把 height 写入 s、latest、帧、height；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_latest_frame.height = height;

    /* 这里把 len 写入 s、latest、帧、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_latest_frame.len = len;

    /* 这里把 s_submit_seq 写入 s、latest、帧、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_latest_frame.seq = s_submit_seq;

    /* 这里把 app_vision_now_ms() 写入 s、latest、帧、节拍、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_latest_frame.tick_ms = app_vision_now_ms();


    /* 这里把 s_submit_seq 写入 s、提交、slot、info、seq；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.seq = s_submit_seq;

    /* 这里把 s_latest_frame.tick_ms 写入 s、提交、slot、info、节拍、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_submit_slot.info.tick_ms = s_latest_frame.tick_ms;

    /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
    memcpy(&s_gray_slot, &s_submit_slot, sizeof(app_vision_gray_slot_t));

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_vision_mux);


    /* 这里开始判断条件 !s_first_submit_logged；只有条件成立，后面的分支代码才会执行。 */
    if (!s_first_submit_logged) {

        /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
        ESP_LOGI(TAG,
                 /* 这里把 %lux%lu gray=%dx%d len=%lu", 写入 first、gray、帧、就绪、源；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                 "first gray frame ready: src=%lux%lu gray=%dx%d len=%lu",
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned long)width,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned long)height,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 VISION_GRAY_WIDTH,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 VISION_GRAY_HEIGHT,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 (unsigned long)len);

        /* 这里把 true 写入 s、first、提交、logged；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_first_submit_logged = true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
