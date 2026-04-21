/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */







/* 引入本项目的 app_ctrl 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ctrl.h"

/* 引入 inttypes.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <inttypes.h>
/* 引入 stdbool.h；标准布尔类型头文件；这里把 true/false 和 bool 定义好，方便表达开关状态。 */
#include <stdbool.h>
/* 引入 stdio.h；标准输入输出头文件；常见的 printf、snprintf 等格式化输出接口都在这里声明。 */
#include <stdio.h>
/* 引入 stdlib.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <stdlib.h>
/* 引入 string.h；标准字符串/内存处理头文件；memcpy、memset、strcmp 等基础接口都来自这里。 */
#include <string.h>

/* 引入 freertos/FreeRTOS.h；FreeRTOS 核心头文件；任务、队列、事件组等内核对象的基础定义都依赖它。 */
#include "freertos/FreeRTOS.h"
/* 引入 freertos/task.h；FreeRTOS 任务头文件；xTaskCreate、vTaskDelay、任务通知等接口主要在这里声明。 */
#include "freertos/task.h"

/* 引入 esp_err.h；ESP-IDF 错误码头文件；esp_err_t、ESP_OK、ESP_ERROR_CHECK 等错误处理机制依赖它。 */
#include "esp_err.h"
/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"

/* 引入本项目的 app_ch32_link 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ch32_link.h"
/* 引入本项目的 app_dock_judge 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_dock_judge.h"
/* 引入本项目的 app_task 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_task.h"
/* 引入本项目的 app_ui 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ui.h"
/* 引入本项目的 app_vision 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_vision.h"
/* 引入 bsp/esp-bsp.h；乐鑫 BSP 总头文件；板级资源封装通常会从这里统一引入。 */
#include "bsp/esp-bsp.h"
/* 引入 bsp_display_port.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "bsp_display_port.h"















/* 这里把 "app_ctrl" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_ctrl";




























































































































/* 定义宏 CTRL_TASK_STACK_SIZE；这里把“控制、任务、STACK、大小”集中写成常量 (7 * 1024)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_TASK_STACK_SIZE            (7 * 1024)
/* 定义宏 CTRL_TASK_PRIORITY；这里把“控制、任务、PRIORITY”集中写成常量 5，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_TASK_PRIORITY              5
/* 定义宏 CTRL_TASK_CORE_ID；这里把“控制、任务、核心、ID”集中写成常量 1，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_TASK_CORE_ID               1
/* 定义宏 CTRL_POLL_MS；这里把“控制、POLL、毫秒”集中写成常量 60U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_POLL_MS                    60U

/* 定义宏 CTRL_READY_PROBE_INTERVAL_MS；这里把“控制、就绪、PROBE、INTERVAL、毫秒”集中写成常量 1000U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_READY_PROBE_INTERVAL_MS    1000U
/* 定义宏 CTRL_DOCK_CMD；这里把“控制、接驳、CMD”集中写成常量 ('A')，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_DOCK_CMD                   ('A')
/* 定义宏 CTRL_ACK_WAIT_MS；这里把“控制、ACK、WAIT、毫秒”集中写成常量 2000U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_ACK_WAIT_MS                2000U
/* 定义宏 CTRL_BUSY_TIMEOUT_MS；这里把“控制、BUSY、超时、毫秒”集中写成常量 20000U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_BUSY_TIMEOUT_MS            20000U
/* 定义宏 CTRL_NOTICE_SHOW_MS；这里把“控制、NOTICE、SHOW、毫秒”集中写成常量 1600U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_NOTICE_SHOW_MS             1600U
/* 定义宏 CTRL_RETRIGGER_COOLDOWN_MS；这里把“控制、RETRIGGER、COOLDOWN、毫秒”集中写成常量 1800U，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_RETRIGGER_COOLDOWN_MS      1800U
/* 定义宏 CTRL_AUTO_DOCK_ENABLE；这里把“控制、AUTO、接驳、使能”集中写成常量 (1)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define CTRL_AUTO_DOCK_ENABLE           (1)



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这里先定义变量 inited，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool inited;

    /* 这里先定义变量 ch32_ready，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool ch32_ready;

    /* 这里先定义变量 dock_busy，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool dock_busy;
    /* 这里先定义变量 cargo_wait_window_seen，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool cargo_wait_window_seen;

    /* 这里先定义变量 has_weight，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool has_weight;

    /* 这里先定义变量 last_weight_g，类型是 int32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    int32_t last_weight_g;


    /* 这里先定义变量 last_proto_flags，类型是 uint16_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint16_t last_proto_flags;

    /* 这里先定义变量 last_proto_stage，类型是 app_ch32_proto_stage_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_ch32_proto_stage_t last_proto_stage;

    /* 这里先定义变量 last_proto_error，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t last_proto_error;


    /* 这里先定义变量 applied_target_id，类型是 uint16_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint16_t applied_target_id;

    /* 这里先定义变量 last_ready_probe_ms，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t last_ready_probe_ms;

    /* 这里先定义变量 busy_deadline_ms，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t busy_deadline_ms;

    /* 这里先定义变量 notice_deadline_ms，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t notice_deadline_ms;

    /* 这里先定义变量 retrigger_deadline_ms，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t retrigger_deadline_ms;


    /* 这里先定义变量 notice，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char notice[96];

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_ctrl_runtime_t;


/* 这里定义变量 s_ctrl_task，类型是 static TaskHandle_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
static TaskHandle_t s_ctrl_task = NULL;

/* 这里定义变量 s_ctrl_mux，类型是 static portMUX_TYPE，并且在声明时就把初值设成 portMUX_INITIALIZER_UNLOCKED；这样后面第一次使用它时就是一个确定状态。 */
static portMUX_TYPE s_ctrl_mux = portMUX_INITIALIZER_UNLOCKED;

/* 这里定义变量 s_rt，类型是 static app_ctrl_runtime_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_ctrl_runtime_t s_rt = {0};






/* 这里开始定义函数 app_ctrl_now_ms；返回类型是 static inline uint32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline uint32_t app_ctrl_now_ms(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_deadline_active；返回类型是 static inline bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline bool app_ctrl_deadline_active(uint32_t deadline_ms, uint32_t now_ms)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (deadline_ms != 0U) && ((int32_t)(deadline_ms - now_ms) > 0) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (deadline_ms != 0U) && ((int32_t)(deadline_ms - now_ms) > 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_set_notice_locked；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_set_notice_locked(const char *text, uint32_t hold_ms)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 text == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (text == NULL) {

        /* 这里把 '\0' 写入 s、rt、notice、0；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.notice[0] = '\0';

        /* 这里把 0 写入 s、rt、notice、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.notice_deadline_ms = 0;

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(s_rt.notice, text, sizeof(s_rt.notice));

    /* 这里把 app_ctrl_now_ms() + hold_ms 写入 s、rt、notice、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.notice_deadline_ms = app_ctrl_now_ms() + hold_ms;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_set_notice；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_set_notice(const char *text, uint32_t hold_ms)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_ctrl_mux);

    /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ctrl_set_notice_locked(text, hold_ms);

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_ctrl_mux);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_start_retrigger_cooldown_locked；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_start_retrigger_cooldown_locked(uint32_t hold_ms)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 app_ctrl_now_ms() + hold_ms 写入 s、rt、retrigger、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.retrigger_deadline_ms = app_ctrl_now_ms() + hold_ms;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_line_has_done_keyword；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_line_has_done_keyword(const char *line)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 line == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (line == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    return (strstr(line, "FLOW_DONE") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "FULLFLOW_DONE") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "ALL_DONE") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "CYCLE_DONE") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "COMPLETE") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "SAFE_LOCKED") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "FLOW_OK") != NULL) ||

           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "IDLE") != NULL);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}

/* 这里开始定义函数 app_ctrl_line_indicates_cargo_wait_window；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_line_indicates_cargo_wait_window(const char *line)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 line == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (line == NULL) {
        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    return (strstr(line, "TRAY_EXTENDED") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "WAITING_CARGO") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "tray extended") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "waiting cargo") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "drawer has been fully extended") != NULL) ||
           /* 调用函数 strstr；从名字看，它承担的职责和“strstr”有关，后续行为取决于这个接口的返回结果或副作用。 */
           (strstr(line, "tray has been fully extended") != NULL);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_proto_stage_is_busy；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_proto_stage_is_busy(app_ch32_proto_stage_t stage)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (stage) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_DOOR_OPENING:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_DOOR_OPENED:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_EXTENDING:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_EXTENDED:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_WAITING_CARGO:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_CARGO_DETECTED:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_RETRACTING:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_RETRACTED:

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_DOOR_CLOSING:

            /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return true;

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_proto_stage_is_cargo_wait_window；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_proto_stage_is_cargo_wait_window(app_ch32_proto_stage_t stage)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    return (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
           /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
           (stage == APP_CH32_STAGE_WAITING_CARGO);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}

/* 这里开始定义函数 app_ctrl_proto_flags_indicate_tray_out；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_proto_flags_indicate_tray_out(uint16_t flags)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里把 (flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (flags & APP_CH32_FLAG_LIMIT_TRAY_OUT) != 0U;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}

/* 这里开始定义函数 app_ctrl_proto_error_is_cargo_wait_soft；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_proto_error_is_cargo_wait_soft(uint8_t proto_error)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    return (proto_error == APP_CH32_ERR_TIMEOUT) ||
           /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
           (proto_error == APP_CH32_ERR_WEIGHT);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 调用本项目模块接口 app_ctrl_proto_stage_status_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
static const char *app_ctrl_proto_stage_status_text(app_ch32_proto_stage_t stage)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (stage) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_IDLE:            return "dock: CH32 idle";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_READY:           return "dock: CH32 ready";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_DOOR_OPENING:    return "dock: door opening";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_DOOR_OPENED:     return "dock: door opened";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_EXTENDING:  return "dock: tray extending";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_EXTENDED:   return "dock: tray extended";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_WAITING_CARGO:   return "dock: waiting cargo";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_CARGO_DETECTED:  return "dock: cargo detected";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_RETRACTING: return "dock: tray retracting";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_TRAY_RETRACTED:  return "dock: tray retracted";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_DOOR_CLOSING:    return "dock: door closing";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_SAFE_LOCKED:     return "dock: safe locked";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_COMPLETE:        return "dock: cycle complete";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_FAULT:           return "dock: CH32 fault";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_CH32_STAGE_UNKNOWN:

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:                             return "dock: CH32 online";
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_proto_stage_uses_busy_deadline；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_proto_stage_uses_busy_deadline(app_ch32_proto_stage_t stage)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里把 !app_ctrl_proto_stage_is_cargo_wait_window(stage) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return !app_ctrl_proto_stage_is_cargo_wait_window(stage);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_is_soft_waiting_cargo_error；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_ctrl_is_soft_waiting_cargo_error(app_ch32_proto_stage_t prev_stage,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 uint16_t prev_flags,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 app_ch32_proto_stage_t stage,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 uint16_t flags,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 uint8_t proto_error,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 bool cargo_wait_window_seen)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里定义变量 tray_out_now，类型是 const bool，并且在声明时就把初值设成 app_ctrl_proto_flags_indicate_tray_out(flags)；这样后面第一次使用它时就是一个确定状态。 */
    const bool tray_out_now = app_ctrl_proto_flags_indicate_tray_out(flags);
    /* 这里定义变量 tray_out_before，类型是 const bool，并且在声明时就把初值设成 app_ctrl_proto_flags_indicate_tray_out(prev_flags)；这样后面第一次使用它时就是一个确定状态。 */
    const bool tray_out_before = app_ctrl_proto_flags_indicate_tray_out(prev_flags);

    /* 这里开始判断条件 !app_ctrl_proto_error_is_cargo_wait_soft(proto_error)；只有条件成立，后面的分支代码才会执行。 */
    if (!app_ctrl_proto_error_is_cargo_wait_soft(proto_error)) {
        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 cargo_wait_window_seen；只有条件成立，后面的分支代码才会执行。 */
    if (cargo_wait_window_seen) {
        /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 app_ctrl_proto_stage_is_cargo_wait_window(stage；只有条件成立，后面的分支代码才会执行。 */
    if (app_ctrl_proto_stage_is_cargo_wait_window(stage) ||
        /* 调用本项目模块接口 app_ctrl_proto_stage_is_cargo_wait_window；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_proto_stage_is_cargo_wait_window(prev_stage)) {
        /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 (stage == APP_CH32_STAGE_FAULT；只有条件成立，后面的分支代码才会执行。 */
    if ((stage == APP_CH32_STAGE_FAULT) &&
        /* 调用本项目模块接口 app_ctrl_proto_stage_is_cargo_wait_window；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (app_ctrl_proto_stage_is_cargo_wait_window(prev_stage) || tray_out_now || tray_out_before)) {
        /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    return tray_out_now &&
           /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
           ((stage == APP_CH32_STAGE_TRAY_EXTENDING) ||
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            (stage == APP_CH32_STAGE_TRAY_EXTENDED) ||
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            (stage == APP_CH32_STAGE_WAITING_CARGO));
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_ctrl_hold_waiting_cargo_locked；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_hold_waiting_cargo_locked(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里把 true 写入 s、rt、cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.cargo_wait_window_seen = true;
    /* 这里把 APP_CH32_STAGE_WAITING_CARGO 写入 s、rt、last、proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
    /* 这里把 APP_CH32_ERR_NONE 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_proto_error = APP_CH32_ERR_NONE;
    /* 这里把 true 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.dock_busy = true;
    /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.busy_deadline_ms = 0;
    /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_ctrl_compose_detail；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    bool has_weight,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    int32_t weight_g,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    app_ch32_proto_stage_t proto_stage,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    char *buf,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    size_t buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 (dock == NULL) || (buf == NULL) || (buf_len == 0U)；只有条件成立，后面的分支代码才会执行。 */
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U)) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->vision_valid；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->vision_valid) {

        /* 这里开始判断条件 dock->state != APP_DOCK_STATE_SEARCHING；只有条件成立，后面的分支代码才会执行。 */
        if (dock->state != APP_DOCK_STATE_SEARCHING) {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ldmm e:%.1f stage:%s",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)dock->invalid_hold_count,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)dock->lost_count,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (long)dock->dx,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (long)dock->dy,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (long)dock->est_distance_mm,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (double)dock->filtered_edge_px,
                     /* 调用本项目模块接口 app_ch32_link_proto_stage_name；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                     app_ch32_link_proto_stage_name(proto_stage));
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(buf,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             buf_len,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "dock dbg: id:%u dx:%ld dy:%ld z:%ldmm e:%.1f ang:%d st:%u score:%u wt:%s%ldg",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)dock->tag_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)dock->dx,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)dock->dy,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long)dock->est_distance_mm,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (double)dock->filtered_edge_px,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (int)dock->angle_deg,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)dock->stable_count,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)dock->hover_score,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             has_weight ? "" : "-",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             has_weight ? (long)weight_g : 0L);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_compose_guidance；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_compose_guidance(const app_dock_judge_result_t *dock,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      char *buf,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      size_t buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 (dock == NULL) || (buf == NULL) || (buf_len == 0U)；只有条件成立，后面的分支代码才会执行。 */
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U)) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->vision_valid；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->vision_valid) {

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf, buf_len, "dock: searching target");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->target_id_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->target_id_ok) {

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf, buf_len, "dock: wrong tag id");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->centered_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->centered_ok) {

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf, buf_len, "dock: align target center");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->near_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->near_ok) {

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf, buf_len, "dock: move target closer");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->stable_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->stable_ok) {

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf, buf_len, "dock: hold hover stable");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !dock->distance_ok；只有条件成立，后面的分支代码才会执行。 */
    if (!dock->distance_ok) {

        /* 这里开始判断条件 dock->est_distance_mm > 0；只有条件成立，后面的分支代码才会执行。 */
        if (dock->est_distance_mm > 0) {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (dock->est_distance_mm < 260) ? "dock: target too near" : "dock: target too far");
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "dock: wait valid distance");
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_dock_judge_format_status；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_dock_judge_format_status(dock, buf, buf_len);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_ctrl_compose_task_status；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_compose_task_status(const app_task_snapshot_t *task,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         const app_dock_judge_result_t *dock,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         bool ch32_ready,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         char *buf,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         size_t buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 buf == NULL || buf_len == 0U || task == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (buf == NULL || buf_len == 0U || task == NULL) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (task->state) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_CONFIGURED:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这里把 %u / remote ready" : "task: target=%u / wait CH32", 写入 ch32、就绪、任务、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                     ch32_ready ? "task: target=%u / remote ready" : "task: target=%u / wait CH32",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)task->target_id);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_WAIT_APPROACH: {

            /* 这里定义变量 guide，类型是 char，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
            char guide[72] = {0};

            /* 调用本项目模块接口 app_ctrl_compose_guidance；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_compose_guidance(dock, guide, sizeof(guide));

            /* 这里把 %u / %s", (unsigned)task->target_id, guide) 写入 snprintf、缓冲区、缓冲区、长度、任务、wait、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            snprintf(buf, buf_len, "task: wait id=%u / %s", (unsigned)task->target_id, guide);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_AUTH_PASSED:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这里把 %u", 写入 任务、auth、passed、matched、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                     "task: auth passed / matched id=%u",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)(task->matched_tag_id != 0U ? task->matched_tag_id : task->target_id));

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_DOCKING:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "task: docking in progress");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_COMPLETED:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这里把 %u", 写入 任务、completed、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                     "task: completed / target=%u",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     (unsigned)task->target_id);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_FAULT:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     buf_len,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     "task: fault / %s",
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     task->note[0] != '\0' ? task->note : "check CH32");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_CANCELLED:

            /* 这里把 %u", (unsigned)task->target_id) 写入 snprintf、缓冲区、缓冲区、长度、任务、cancelled、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            snprintf(buf, buf_len, "task: cancelled / target=%u", (unsigned)task->target_id);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_IDLE:

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(buf, buf_len, "task: idle");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_apply_proto_msg_locked；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_apply_proto_msg_locked(const app_ch32_line_t *msg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 msg == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (msg == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里定义变量 prev_proto_stage，类型是 const app_ch32_proto_stage_t，并且在声明时就把初值设成 s_rt.last_proto_stage；这样后面第一次使用它时就是一个确定状态。 */
    const app_ch32_proto_stage_t prev_proto_stage = s_rt.last_proto_stage;
    /* 这里定义变量 prev_proto_flags，类型是 const uint16_t，并且在声明时就把初值设成 s_rt.last_proto_flags；这样后面第一次使用它时就是一个确定状态。 */
    const uint16_t prev_proto_flags = s_rt.last_proto_flags;
    /* 这里定义变量 prev_cargo_wait_window_seen，类型是 const bool，并且在声明时就把初值设成 s_rt.cargo_wait_window_seen；这样后面第一次使用它时就是一个确定状态。 */
    const bool prev_cargo_wait_window_seen = s_rt.cargo_wait_window_seen;


    /* 这里把 app_ch32_link_is_ready() 写入 s、rt、ch32、就绪；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.ch32_ready = app_ch32_link_is_ready();


    /* 这里开始判断条件 msg->payload_len >= 8U；只有条件成立，后面的分支代码才会执行。 */
    if (msg->payload_len >= 8U) {

        /* 这里把 msg->proto_weight_g 写入 s、rt、last、重量、g；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.last_weight_g = msg->proto_weight_g;

        /* 这里把 true 写入 s、rt、has、重量；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.has_weight = true;

        /* 这里把 msg->proto_flags 写入 s、rt、last、proto、flags；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.last_proto_flags = msg->proto_flags;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 (msg->type == APP_CH32_LINE_PROTO_STATUS；只有条件成立，后面的分支代码才会执行。 */
    if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        /* 这里开始判断条件 msg->payload_len >= 8U；只有条件成立，后面的分支代码才会执行。 */
        if (msg->payload_len >= 8U) {
            /* 这里把 msg->proto_flags 写入 s、rt、last、proto、flags；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_proto_flags = msg->proto_flags;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
        /* 这里开始判断条件 app_ctrl_proto_stage_is_cargo_wait_window(msg->proto_stage；只有条件成立，后面的分支代码才会执行。 */
        if (app_ctrl_proto_stage_is_cargo_wait_window(msg->proto_stage) ||
            /* 调用本项目模块接口 app_ctrl_proto_flags_indicate_tray_out；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            ((msg->payload_len >= 8U) && app_ctrl_proto_flags_indicate_tray_out(msg->proto_flags))) {
            /* 这里把 true 写入 s、rt、cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.cargo_wait_window_seen = true;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里开始判断条件 s_rt.last_proto_stage != msg->proto_stage；只有条件成立，后面的分支代码才会执行。 */
        if (s_rt.last_proto_stage != msg->proto_stage) {
            /* 这里把 msg->proto_stage 写入 s、rt、last、proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_proto_stage = msg->proto_stage;

            /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice_locked(app_ctrl_proto_stage_status_text(msg->proto_stage), CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 (msg->type == APP_CH32_LINE_PROTO_ERROR) || (msg->proto_stage == APP_CH32_STAGE_FAULT)；只有条件成立，后面的分支代码才会执行。 */
        if ((msg->type == APP_CH32_LINE_PROTO_ERROR) || (msg->proto_stage == APP_CH32_STAGE_FAULT)) {

            /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
            if (app_ctrl_is_soft_waiting_cargo_error(prev_proto_stage,
                                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                     prev_proto_flags,
                                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                     msg->proto_stage,
                                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                     msg->proto_flags,
                                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                     msg->proto_detail,
                                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                     prev_cargo_wait_window_seen)) {
                /* 调用本项目模块接口 app_ctrl_hold_waiting_cargo_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_ctrl_hold_waiting_cargo_locked();
                /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
                return;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里把 msg->proto_detail 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_proto_error = msg->proto_detail;

            /* 这里把 false 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.dock_busy = false;

            /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.busy_deadline_ms = 0;

            /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(s_rt.notice,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     sizeof(s_rt.notice),
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     "dock: CH32 err %s",
                     /* 调用本项目模块接口 app_ch32_link_proto_error_name；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                     app_ch32_link_proto_error_name(msg->proto_detail));

            /* 这里把 app_ctrl_now_ms() + CTRL_NOTICE_SHOW_MS 写入 s、rt、notice、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.notice_deadline_ms = app_ctrl_now_ms() + CTRL_NOTICE_SHOW_MS;

            /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
            return;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 ((msg->proto_flags & APP_CH32_FLAG_BUSY) != 0U) || app_ctrl_proto_stage_is_busy(msg->proto_stage)；只有条件成立，后面的分支代码才会执行。 */
        if (((msg->proto_flags & APP_CH32_FLAG_BUSY) != 0U) || app_ctrl_proto_stage_is_busy(msg->proto_stage)) {

            /* 这里把 true 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.dock_busy = true;

            /* 这里开始判断条件 app_ctrl_proto_stage_uses_busy_deadline(msg->proto_stage)；只有条件成立，后面的分支代码才会执行。 */
            if (app_ctrl_proto_stage_uses_busy_deadline(msg->proto_stage)) {
                /* 这里把 app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.busy_deadline_ms = app_ctrl_now_ms() + CTRL_BUSY_TIMEOUT_MS;
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {
                /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.busy_deadline_ms = 0;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
            return;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 (msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED；只有条件成立，后面的分支代码才会执行。 */
        if ((msg->proto_stage == APP_CH32_STAGE_SAFE_LOCKED) ||
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            (msg->proto_stage == APP_CH32_STAGE_COMPLETE) ||
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            (msg->proto_stage == APP_CH32_STAGE_IDLE) ||
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            (msg->proto_stage == APP_CH32_STAGE_READY)) {
            /* 这里把 false 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.dock_busy = false;

            /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.busy_deadline_ms = 0;

            /* 这里把 APP_CH32_ERR_NONE 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_proto_error = APP_CH32_ERR_NONE;

            /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_on_ch32_line；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ctrl_on_ch32_line(const app_ch32_line_t *msg, void *user_ctx)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)user_ctx;


    /* 这里开始判断条件 msg == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (msg == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_ctrl_mux);


    /* 这里开始判断条件 msg->is_proto；只有条件成立，后面的分支代码才会执行。 */
    if (msg->is_proto) {

        /* 调用本项目模块接口 app_ctrl_apply_proto_msg_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_apply_proto_msg_locked(msg);

        /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
        taskEXIT_CRITICAL(&s_ctrl_mux);

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 msg->type == APP_CH32_LINE_STATUS；只有条件成立，后面的分支代码才会执行。 */
    if (msg->type == APP_CH32_LINE_STATUS) {

        /* 这里开始判断条件 strstr(msg->line, "CH32_READY") != NULL；只有条件成立，后面的分支代码才会执行。 */
        if (strstr(msg->line, "CH32_READY") != NULL) {

            /* 这里把 true 写入 s、rt、ch32、就绪；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.ch32_ready = true;

            /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice_locked("dock: CH32 ready", CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里把 strstr(msg->line, "WEIGHT=") 写入 const、char、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        const char *w = strstr(msg->line, "WEIGHT=");

        /* 这里开始判断条件 w != NULL；只有条件成立，后面的分支代码才会执行。 */
        if (w != NULL) {

            /* 这里把 (int32_t)strtol(w + 7, NULL, 10) 写入 s、rt、last、重量、g；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_weight_g = (int32_t)strtol(w + 7, NULL, 10);

            /* 这里把 true 写入 s、rt、has、重量；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.has_weight = true;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 app_ctrl_line_indicates_cargo_wait_window(msg->line)；只有条件成立，后面的分支代码才会执行。 */
        if (app_ctrl_line_indicates_cargo_wait_window(msg->line)) {
            /* 这里把 true 写入 s、rt、cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.cargo_wait_window_seen = true;
            /* 这里把 true 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.dock_busy = true;
            /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.busy_deadline_ms = 0;
            /* 这里把 APP_CH32_STAGE_WAITING_CARGO 写入 s、rt、last、proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_proto_stage = APP_CH32_STAGE_WAITING_CARGO;
            /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice_locked("dock: waiting cargo", CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里开始判断条件 app_ctrl_line_has_done_keyword(msg->line)；只有条件成立，后面的分支代码才会执行。 */
        if (app_ctrl_line_has_done_keyword(msg->line)) {

            /* 这里把 false 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.dock_busy = false;
            /* 这里把 false 写入 s、rt、cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.cargo_wait_window_seen = false;

            /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.busy_deadline_ms = 0;

            /* 这里把 APP_CH32_ERR_NONE 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_proto_error = APP_CH32_ERR_NONE;

            /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);

            /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice_locked("dock: CH32 cycle done", CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    } else if (msg->type == APP_CH32_LINE_ERROR) {

        /* 这里把 false 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.dock_busy = false;

        /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.busy_deadline_ms = 0;

        /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);

        /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_set_notice_locked("dock: CH32 error", CTRL_NOTICE_SHOW_MS);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_ctrl_mux);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_handle_touch；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_handle_touch(const app_task_snapshot_t *task,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  bool dock_busy,
                                  /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                  uint32_t now_ms)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)task;
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)dock_busy;
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)now_ms;
    /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
    return;
/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if 0
    /* 这里定义变量 x，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t x = 0;

    /* 这里定义变量 y，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t y = 0;


    /* 这里开始判断条件 !app_display_touch_read(&x, &y)；只有条件成立，后面的分支代码才会执行。 */
    if (!app_display_touch_read(&x, &y)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_ui_set_coord；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_coord(x, y);


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_ctrl_mux);

    /* 这里定义变量 last_touch_ms，类型是 const uint32_t，并且在声明时就把初值设成 s_rt.last_touch_ms；这样后面第一次使用它时就是一个确定状态。 */
    const uint32_t last_touch_ms = s_rt.last_touch_ms;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_ctrl_mux);


    /* 这里开始判断条件 now_ms - last_touch_ms < CTRL_TOUCH_DEBOUNCE_MS；只有条件成立，后面的分支代码才会执行。 */
    if (now_ms - last_touch_ms < CTRL_TOUCH_DEBOUNCE_MS) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_ctrl_mux);

    /* 这里把 now_ms 写入 s、rt、last、touch、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_touch_ms = now_ms;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_ctrl_mux);


    /* 这里定义变量 w，类型是 const int32_t，并且在声明时就把初值设成 BSP_LCD_H_RES；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t w = BSP_LCD_H_RES;

    /* 这里定义变量 h，类型是 const int32_t，并且在声明时就把初值设成 BSP_LCD_V_RES；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t h = BSP_LCD_V_RES;

    /* 这里定义变量 top_half，类型是 const bool，并且在声明时就把初值设成 (y < (h / 2))；这样后面第一次使用它时就是一个确定状态。 */
    const bool top_half = (y < (h / 2));

    /* 这里定义变量 left_half，类型是 const bool，并且在声明时就把初值设成 (x < (w / 2))；这样后面第一次使用它时就是一个确定状态。 */
    const bool left_half = (x < (w / 2));


    /* 这里开始判断条件 top_half；只有条件成立，后面的分支代码才会执行。 */
    if (top_half) {

        /* 这里开始判断条件 dock_busy || (task->state == APP_TASK_STATE_DOCKING)；只有条件成立，后面的分支代码才会执行。 */
        if (dock_busy || (task->state == APP_TASK_STATE_DOCKING)) {

            /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice("cfg locked while docking", CTRL_NOTICE_SHOW_MS);

            /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
            return;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里定义变量 target，类型是 uint16_t，并且在声明时就把初值设成 task->target_id；这样后面第一次使用它时就是一个确定状态。 */
        uint16_t target = task->target_id;

        /* 这里开始判断条件 left_half；只有条件成立，后面的分支代码才会执行。 */
        if (left_half) {

            /* 这里把 (target > CTRL_TASK_ID_MIN) ? (uint16_t)(target - 1U) : CTRL_TASK_ID_MIN 写入 目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            target = (target > CTRL_TASK_ID_MIN) ? (uint16_t)(target - 1U) : CTRL_TASK_ID_MIN;
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 这里把 (target < CTRL_TASK_ID_MAX) ? (uint16_t)(target + 1U) : CTRL_TASK_ID_MAX 写入 目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            target = (target < CTRL_TASK_ID_MAX) ? (uint16_t)(target + 1U) : CTRL_TASK_ID_MAX;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 app_task_set_target_id(target, true) == ESP_OK；只有条件成立，后面的分支代码才会执行。 */
        if (app_task_set_target_id(target, true) == ESP_OK) {

            /* 这里先定义变量 msg，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
            char msg[64];

            /* 这里把 > %u", (unsigned)target) 写入 snprintf、消息、sizeof、消息、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            snprintf(msg, sizeof(msg), "target id => %u", (unsigned)target);

            /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice(msg, CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 left_half；只有条件成立，后面的分支代码才会执行。 */
    if (left_half) {

        /* 这里开始判断条件 dock_busy；只有条件成立，后面的分支代码才会执行。 */
        if (dock_busy) {

            /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice("dock busy, cannot start", CTRL_NOTICE_SHOW_MS);

            /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
            return;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里开始判断条件 app_task_start_local() == ESP_OK；只有条件成立，后面的分支代码才会执行。 */
        if (app_task_start_local() == ESP_OK) {

            /* 这里先定义变量 msg，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
            char msg[64];

            /* 这里把 %u", (unsigned)task->target_id) 写入 snprintf、消息、sizeof、消息、任务、armed、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            snprintf(msg, sizeof(msg), "task armed / target=%u", (unsigned)task->target_id);

            /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice(msg, CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 dock_busy；只有条件成立，后面的分支代码才会执行。 */
    if (dock_busy) {

        /* 调用本项目模块接口 app_ch32_link_send_cmd；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_ch32_link_send_cmd('S');

        /* 调用本项目模块接口 app_task_cancel；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_cancel("manual abort");

        /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_set_notice("manual abort sent", CTRL_NOTICE_SHOW_MS);
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 调用本项目模块接口 app_task_reset_idle；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_reset_idle();

        /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_set_notice("task reset", CTRL_NOTICE_SHOW_MS);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_task；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ctrl_task(void *arg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)arg;


    /* 这里定义变量 prev_ready_level，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool prev_ready_level = false;

    /* 这里定义变量 prev_dock_busy，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool prev_dock_busy = false;


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (1) {

        /* 这里定义变量 now_ms，类型是 const uint32_t，并且在声明时就把初值设成 app_ctrl_now_ms()；这样后面第一次使用它时就是一个确定状态。 */
        const uint32_t now_ms = app_ctrl_now_ms();


        /* 这里定义变量 vision，类型是 app_vision_result_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        app_vision_result_t vision = {0};

        /* 这里定义变量 dock，类型是 app_dock_judge_result_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        app_dock_judge_result_t dock = {0};

        /* 这里定义变量 task，类型是 app_task_snapshot_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        app_task_snapshot_t task = {0};


        /* 调用本项目模块接口 app_vision_get_latest_result；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_vision_get_latest_result(&vision);

        /* 调用本项目模块接口 app_dock_judge_process；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_dock_judge_process(&vision, &dock);

        /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_task_get_snapshot(&task);


        /* 这里定义变量 ch32_ready，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
        bool ch32_ready = false;

        /* 这里定义变量 dock_busy，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
        bool dock_busy = false;

        /* 这里定义变量 has_weight，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
        bool has_weight = false;

        /* 这里定义变量 weight_g，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        int32_t weight_g = 0;

        /* 这里定义变量 proto_stage，类型是 app_ch32_proto_stage_t，并且在声明时就把初值设成 APP_CH32_STAGE_UNKNOWN；这样后面第一次使用它时就是一个确定状态。 */
        app_ch32_proto_stage_t proto_stage = APP_CH32_STAGE_UNKNOWN;
        /* 这里定义变量 proto_flags，类型是 uint16_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint16_t proto_flags = 0;

        /* 这里定义变量 proto_error，类型是 uint8_t，并且在声明时就把初值设成 APP_CH32_ERR_NONE；这样后面第一次使用它时就是一个确定状态。 */
        uint8_t proto_error = APP_CH32_ERR_NONE;
        /* 这里定义变量 cargo_wait_window_seen，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
        bool cargo_wait_window_seen = false;

        /* 这里定义变量 last_probe_ms，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint32_t last_probe_ms = 0;

        /* 这里定义变量 busy_deadline_ms，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint32_t busy_deadline_ms = 0;

        /* 这里定义变量 notice_deadline_ms，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint32_t notice_deadline_ms = 0;

        /* 这里定义变量 retrigger_deadline_ms，类型是 uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint32_t retrigger_deadline_ms = 0;

        /* 这里定义变量 notice，类型是 char，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        char notice[96] = {0};

        /* 这里定义变量 applied_target_id，类型是 uint16_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
        uint16_t applied_target_id = 0;


        /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
        taskENTER_CRITICAL(&s_ctrl_mux);

        /* 这里把 app_ch32_link_is_ready() 写入 s、rt、ch32、就绪；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.ch32_ready = app_ch32_link_is_ready();

        /* 这里把 s_rt.ch32_ready 写入 ch32、就绪；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ch32_ready = s_rt.ch32_ready;

        /* 这里把 s_rt.dock_busy 写入 接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        dock_busy = s_rt.dock_busy;
        /* 这里把 s_rt.cargo_wait_window_seen 写入 cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        cargo_wait_window_seen = s_rt.cargo_wait_window_seen;

        /* 这里把 s_rt.has_weight 写入 has、重量；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        has_weight = s_rt.has_weight;

        /* 这里把 s_rt.last_weight_g 写入 重量、g；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        weight_g = s_rt.last_weight_g;

        /* 这里把 s_rt.last_proto_stage 写入 proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        proto_stage = s_rt.last_proto_stage;
        /* 这里把 s_rt.last_proto_flags 写入 proto、flags；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        proto_flags = s_rt.last_proto_flags;

        /* 这里把 s_rt.last_proto_error 写入 proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        proto_error = s_rt.last_proto_error;

        /* 这里把 s_rt.last_ready_probe_ms 写入 last、probe、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        last_probe_ms = s_rt.last_ready_probe_ms;

        /* 这里把 s_rt.busy_deadline_ms 写入 busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        busy_deadline_ms = s_rt.busy_deadline_ms;

        /* 这里把 s_rt.notice_deadline_ms 写入 notice、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        notice_deadline_ms = s_rt.notice_deadline_ms;

        /* 这里把 s_rt.retrigger_deadline_ms 写入 retrigger、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        retrigger_deadline_ms = s_rt.retrigger_deadline_ms;

        /* 这里把 s_rt.applied_target_id 写入 applied、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        applied_target_id = s_rt.applied_target_id;

        /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
        strlcpy(notice, s_rt.notice, sizeof(notice));

        /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
        taskEXIT_CRITICAL(&s_ctrl_mux);


        /* 这里开始判断条件 (task.target_dirty) || (applied_target_id != task.target_id)；只有条件成立，后面的分支代码才会执行。 */
        if ((task.target_dirty) || (applied_target_id != task.target_id)) {

            /* 这里开始判断条件 app_dock_judge_set_target_id(task.target_id, true) == ESP_OK；只有条件成立，后面的分支代码才会执行。 */
            if (app_dock_judge_set_target_id(task.target_id, true) == ESP_OK) {

                /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
                taskENTER_CRITICAL(&s_ctrl_mux);

                /* 这里把 task.target_id 写入 s、rt、applied、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.applied_target_id = task.target_id;

                /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
                taskEXIT_CRITICAL(&s_ctrl_mux);

                /* 这里把 > %u", (unsigned)task.target_id) 写入 ESP、LOGI、标签、applied、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                ESP_LOGI(TAG, "applied target id => %u", (unsigned)task.target_id);
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 调用本项目模块接口 app_ctrl_handle_touch；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_handle_touch(&task, dock_busy, now_ms);


        /* 这里开始判断条件 !ch32_ready && (now_ms - last_probe_ms >= CTRL_READY_PROBE_INTERVAL_MS)；只有条件成立，后面的分支代码才会执行。 */
        if (!ch32_ready && (now_ms - last_probe_ms >= CTRL_READY_PROBE_INTERVAL_MS)) {

            /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
            taskENTER_CRITICAL(&s_ctrl_mux);

            /* 这里把 now_ms 写入 s、rt、last、就绪、probe、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_rt.last_ready_probe_ms = now_ms;

            /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
            taskEXIT_CRITICAL(&s_ctrl_mux);

            /* 调用本项目模块接口 app_ch32_link_probe_ready；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            (void)app_ch32_link_probe_ready(200);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 dock_busy && (busy_deadline_ms != 0U) && ((int32_t)(now_ms - busy_deadline_ms) >= 0)；只有条件成立，后面的分支代码才会执行。 */
        if (dock_busy && (busy_deadline_ms != 0U) && ((int32_t)(now_ms - busy_deadline_ms) >= 0)) {

            /* 这里开始判断条件 app_ctrl_proto_stage_is_cargo_wait_window(proto_stage) || cargo_wait_window_seen；只有条件成立，后面的分支代码才会执行。 */
            if (app_ctrl_proto_stage_is_cargo_wait_window(proto_stage) || cargo_wait_window_seen) {
                /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
                taskENTER_CRITICAL(&s_ctrl_mux);
                /* 调用本项目模块接口 app_ctrl_hold_waiting_cargo_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_ctrl_hold_waiting_cargo_locked();
                /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
                taskEXIT_CRITICAL(&s_ctrl_mux);
                /* 这里把 true 写入 接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                dock_busy = true;
                /* 这里把 true 写入 cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                cargo_wait_window_seen = true;
                /* 这里把 APP_CH32_STAGE_WAITING_CARGO 写入 proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                proto_stage = APP_CH32_STAGE_WAITING_CARGO;
                /* 这里把 APP_CH32_ERR_NONE 写入 proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                proto_error = APP_CH32_ERR_NONE;
                /* 这里把 0 写入 busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                busy_deadline_ms = 0;
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
                taskENTER_CRITICAL(&s_ctrl_mux);

                /* 这里把 false 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.dock_busy = false;

                /* 这里把 0 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.busy_deadline_ms = 0;

                /* 这里把 APP_CH32_ERR_TIMEOUT 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_rt.last_proto_error = APP_CH32_ERR_TIMEOUT;

                /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);

                /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_ctrl_set_notice_locked("dock: CH32 timeout", CTRL_NOTICE_SHOW_MS);

                /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
                taskEXIT_CRITICAL(&s_ctrl_mux);

                /* 这里把 false 写入 接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                dock_busy = false;

                /* 这里把 APP_CH32_ERR_TIMEOUT 写入 proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                proto_error = APP_CH32_ERR_TIMEOUT;

                /* 这里开始判断条件 task.active || task.state == APP_TASK_STATE_DOCKING；只有条件成立，后面的分支代码才会执行。 */
                if (task.active || task.state == APP_TASK_STATE_DOCKING) {

                    /* 调用本项目模块接口 app_task_mark_fault；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_task_mark_fault("CH32 timeout");

                    /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    (void)app_task_get_snapshot(&task);
                /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
                }
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
        if (!dock_busy &&
            /* 调用本项目模块接口 app_ctrl_is_soft_waiting_cargo_error；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_is_soft_waiting_cargo_error(proto_stage,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 proto_flags,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 proto_stage,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 proto_flags,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 proto_error,
                                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                 cargo_wait_window_seen)) {
            /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
            taskENTER_CRITICAL(&s_ctrl_mux);
            /* 调用本项目模块接口 app_ctrl_hold_waiting_cargo_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_hold_waiting_cargo_locked();
            /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
            taskEXIT_CRITICAL(&s_ctrl_mux);
            /* 这里把 true 写入 接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            dock_busy = true;
            /* 这里把 true 写入 cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            cargo_wait_window_seen = true;
            /* 这里把 APP_CH32_STAGE_WAITING_CARGO 写入 proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            proto_stage = APP_CH32_STAGE_WAITING_CARGO;
            /* 这里把 APP_CH32_ERR_NONE 写入 proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            proto_error = APP_CH32_ERR_NONE;
            /* 这里把 0 写入 busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            busy_deadline_ms = 0;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里定义变量 ready_level，类型是 const bool，并且在声明时就把初值设成 (dock.state == APP_DOCK_STATE_READY_TO_DOCK)；这样后面第一次使用它时就是一个确定状态。 */
        const bool ready_level = (dock.state == APP_DOCK_STATE_READY_TO_DOCK);

        /* 这里定义变量 retrigger_blocked，类型是 const bool，并且在声明时就把初值设成 app_ctrl_deadline_active(retrigger_deadline_ms, now_ms)；这样后面第一次使用它时就是一个确定状态。 */
        const bool retrigger_blocked = app_ctrl_deadline_active(retrigger_deadline_ms, now_ms);


        /* 这里开始判断条件 task.active && (task.state == APP_TASK_STATE_WAIT_APPROACH) && ready_level；只有条件成立，后面的分支代码才会执行。 */
        if (task.active && (task.state == APP_TASK_STATE_WAIT_APPROACH) && ready_level) {

            /* 调用本项目模块接口 app_task_mark_auth_passed；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_task_mark_auth_passed(dock.tag_id);

            /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            (void)app_task_get_snapshot(&task);

            /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ctrl_set_notice("auth passed / ready to dock", CTRL_NOTICE_SHOW_MS);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if CTRL_AUTO_DOCK_ENABLE

        /* 这里开始判断条件 task.active && !dock_busy && !retrigger_blocked && !prev_ready_level && ready_level；只有条件成立，后面的分支代码才会执行。 */
        if (task.active && !dock_busy && !retrigger_blocked && !prev_ready_level && ready_level) {

            /* 这里开始判断条件 !ch32_ready；只有条件成立，后面的分支代码才会执行。 */
            if (!ch32_ready) {

                /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_ctrl_set_notice("dock: ready but CH32 not ready", CTRL_NOTICE_SHOW_MS);
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
                ESP_LOGI(TAG,
                         /* 这里把 %u dx=%ld dy=%ld z=%ld score=%u)", 写入 就绪、rising、edge、发送、c、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                         "READY rising edge -> send @%c (id=%u dx=%ld dy=%ld z=%ld score=%u)",
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         CTRL_DOCK_CMD,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (unsigned)dock.tag_id,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (long)dock.dx,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (long)dock.dy,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (long)dock.est_distance_mm,
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         (unsigned)dock.hover_score);


                /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_ch32_link_send_cmd_and_wait_ack(CTRL_DOCK_CMD, CTRL_ACK_WAIT_MS)；这样后面第一次使用它时就是一个确定状态。 */
                esp_err_t ret = app_ch32_link_send_cmd_and_wait_ack(CTRL_DOCK_CMD, CTRL_ACK_WAIT_MS);

                /* 这里开始判断条件 ret == ESP_OK；只有条件成立，后面的分支代码才会执行。 */
                if (ret == ESP_OK) {

                    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
                    taskENTER_CRITICAL(&s_ctrl_mux);

                    /* 这里把 true 写入 s、rt、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.dock_busy = true;

                    /* 这里把 false 写入 s、rt、cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.cargo_wait_window_seen = false;
                    /* 这里把 APP_CH32_ERR_NONE 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.last_proto_error = APP_CH32_ERR_NONE;
                    /* 这里把 APP_CH32_STAGE_UNKNOWN 写入 s、rt、last、proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;
                    /* 这里把 0 写入 s、rt、last、proto、flags；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.last_proto_flags = 0;

                    /* 这里把 now_ms + CTRL_BUSY_TIMEOUT_MS 写入 s、rt、busy、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.busy_deadline_ms = now_ms + CTRL_BUSY_TIMEOUT_MS;

                    /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);

                    /* 调用本项目模块接口 app_ctrl_set_notice_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_ctrl_set_notice_locked("dock: CH32 accepted start dock", CTRL_NOTICE_SHOW_MS);

                    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
                    taskEXIT_CRITICAL(&s_ctrl_mux);

                    /* 这里把 true 写入 接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    dock_busy = true;
                    /* 这里把 false 写入 cargo、wait、window、seen；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    cargo_wait_window_seen = false;

                    /* 调用本项目模块接口 app_task_mark_docking_started；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_task_mark_docking_started();

                    /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    (void)app_task_get_snapshot(&task);
                /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
                } else {

                    /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
                    ESP_LOGW(TAG, "send @%c failed: %s", CTRL_DOCK_CMD, esp_err_to_name(ret));

                    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
                    taskENTER_CRITICAL(&s_ctrl_mux);

                    /* 这里把 APP_CH32_ERR_INTERNAL 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                    s_rt.last_proto_error = APP_CH32_ERR_INTERNAL;

                    /* 调用本项目模块接口 app_ctrl_start_retrigger_cooldown_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_ctrl_start_retrigger_cooldown_locked(CTRL_RETRIGGER_COOLDOWN_MS);

                    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
                    taskEXIT_CRITICAL(&s_ctrl_mux);

                    /* 调用本项目模块接口 app_ctrl_set_notice；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_ctrl_set_notice("dock: CH32 ack timeout", CTRL_NOTICE_SHOW_MS);

                    /* 调用本项目模块接口 app_task_mark_fault；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_task_mark_fault("CH32 ack timeout");

                    /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    (void)app_task_get_snapshot(&task);
                /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
                }
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif


        /* 这里开始判断条件 !prev_dock_busy && dock_busy && task.active && task.state != APP_TASK_STATE_DOCKING；只有条件成立，后面的分支代码才会执行。 */
        if (!prev_dock_busy && dock_busy && task.active && task.state != APP_TASK_STATE_DOCKING) {

            /* 调用本项目模块接口 app_task_mark_docking_started；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_task_mark_docking_started();

            /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            (void)app_task_get_snapshot(&task);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 prev_dock_busy && !dock_busy；只有条件成立，后面的分支代码才会执行。 */
        if (prev_dock_busy && !dock_busy) {

            /* 这里开始判断条件 proto_error == APP_CH32_ERR_NONE；只有条件成立，后面的分支代码才会执行。 */
            if (proto_error == APP_CH32_ERR_NONE) {

                /* 这里开始判断条件 task.state == APP_TASK_STATE_DOCKING || task.active；只有条件成立，后面的分支代码才会执行。 */
                if (task.state == APP_TASK_STATE_DOCKING || task.active) {

                    /* 调用本项目模块接口 app_task_mark_completed；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    app_task_mark_completed("dock cycle done");

                    /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                    (void)app_task_get_snapshot(&task);
                /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
                }
            /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
            } else {

                /* 调用本项目模块接口 app_task_mark_fault；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));

                /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                (void)app_task_get_snapshot(&task);
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 (proto_error != APP_CH32_ERR_NONE) && !dock_busy && (task.active || task.state == APP_TASK_STATE_DOCKING)；只有条件成立，后面的分支代码才会执行。 */
        if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy && (task.active || task.state == APP_TASK_STATE_DOCKING)) {

            /* 调用本项目模块接口 app_task_mark_fault；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_task_mark_fault(app_ch32_link_proto_error_name(proto_error));

            /* 调用本项目模块接口 app_task_get_snapshot；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            (void)app_task_get_snapshot(&task);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里把 ready_level 写入 prev、就绪、level；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        prev_ready_level = ready_level;

        /* 这里把 dock_busy 写入 prev、接驳、busy；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        prev_dock_busy = dock_busy;


        /* 这里定义变量 status，类型是 char，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        char status[128] = {0};

        /* 这里定义变量 detail，类型是 char，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        char detail[224] = {0};

        /* 这里定义变量 task_brief，类型是 char，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        char task_brief[96] = {0};


        /* 调用本项目模块接口 app_task_format_brief；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_format_brief(&task, task_brief, sizeof(task_brief));

        /* 调用本项目模块接口 app_ctrl_compose_task_status；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_compose_task_status(&task, &dock, ch32_ready, status, sizeof(status));

        /* 调用本项目模块接口 app_ctrl_compose_detail；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ctrl_compose_detail(&dock, has_weight, weight_g, proto_stage, detail, sizeof(detail));


        /* 这里开始判断条件 dock_busy；只有条件成立，后面的分支代码才会执行。 */
        if (dock_busy) {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(status, sizeof(status), "%s", app_ctrl_proto_stage_status_text(proto_stage));
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 (proto_error != APP_CH32_ERR_NONE) && !dock_busy；只有条件成立，后面的分支代码才会执行。 */
        if ((proto_error != APP_CH32_ERR_NONE) && !dock_busy) {

            /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
            snprintf(status,
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     sizeof(status),
                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                     "dock: CH32 err %s",
                     /* 调用本项目模块接口 app_ch32_link_proto_error_name；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                     app_ch32_link_proto_error_name(proto_error));
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 retrigger_blocked && !dock_busy && (proto_error == APP_CH32_ERR_NONE) && (notice[0] == '\0')；只有条件成立，后面的分支代码才会执行。 */
        if (retrigger_blocked && !dock_busy && (proto_error == APP_CH32_ERR_NONE) && (notice[0] == '\0')) {

            /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
            strlcpy(status, "dock: cooldown / wait next approach", sizeof(status));
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 app_ctrl_deadline_active(notice_deadline_ms, now_ms) && (notice[0] != '\0')；只有条件成立，后面的分支代码才会执行。 */
        if (app_ctrl_deadline_active(notice_deadline_ms, now_ms) && (notice[0] != '\0')) {

            /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
            strlcpy(status, notice, sizeof(status));
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 调用本项目模块接口 app_ui_set_status；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_set_status(status);

        /* 调用本项目模块接口 app_ui_set_vision_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_set_vision_text(task_brief);

        /* 调用本项目模块接口 app_ui_set_dock_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_set_dock_text(detail);

        /* 调用本项目模块接口 app_ui_update_hud；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_update_hud(&vision, &dock);


        /* 让当前 FreeRTOS 任务主动让出 CPU 一段时间；这样不会像 while 死等那样把系统卡住。 */
        vTaskDelay(pdMS_TO_TICKS(CTRL_POLL_MS));
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_init；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_ctrl_init(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_rt.inited；只有条件成立，后面的分支代码才会执行。 */
    if (s_rt.inited) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_ctrl_mux);

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_rt, 0, sizeof(s_rt));

    /* 这里把 APP_CH32_STAGE_UNKNOWN 写入 s、rt、last、proto、stage；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_proto_stage = APP_CH32_STAGE_UNKNOWN;

    /* 这里把 APP_CH32_ERR_NONE 写入 s、rt、last、proto、error；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.last_proto_error = APP_CH32_ERR_NONE;

    /* 这里把 true 写入 s、rt、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.inited = true;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_ctrl_mux);


    /* 这里把 %d)", CTRL_AUTO_DOCK_ENABLE) 写入 ESP、LOGI、标签、控制、初始化、done、auto、接驳；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "ctrl init done (auto_dock=%d)", CTRL_AUTO_DOCK_ENABLE);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ctrl_start；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_ctrl_start(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_rt.inited；只有条件成立，后面的分支代码才会执行。 */
    if (!s_rt.inited) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_ctrl_task != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_ctrl_task != NULL) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 ret，类型是 BaseType_t，并且在声明时就把初值设成 xTaskCreatePinnedToCore(app_ctrl_task,；这样后面第一次使用它时就是一个确定状态。 */
    BaseType_t ret = xTaskCreatePinnedToCore(app_ctrl_task,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             "app_ctrl",
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             CTRL_TASK_STACK_SIZE,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             NULL,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             CTRL_TASK_PRIORITY,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             &s_ctrl_task,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             CTRL_TASK_CORE_ID);


    /* 这里开始判断条件 ret != pdPASS；只有条件成立，后面的分支代码才会执行。 */
    if (ret != pdPASS) {

        /* 这里把 NULL 写入 s、控制、任务；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_ctrl_task = NULL;

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "ctrl task started");

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
