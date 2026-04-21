/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */







/* 引入本项目的 app_cloud 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_cloud.h"

/* 引入 ctype.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <ctype.h>
/* 引入 stdbool.h；标准布尔类型头文件；这里把 true/false 和 bool 定义好，方便表达开关状态。 */
#include <stdbool.h>
/* 引入 stdint.h；标准整数类型头文件；这里提供 uint8_t、uint32_t 这类位宽固定的整数类型，嵌入式里很常用。 */
#include <stdint.h>
/* 引入 stdio.h；标准输入输出头文件；常见的 printf、snprintf 等格式化输出接口都在这里声明。 */
#include <stdio.h>
/* 引入 stdlib.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <stdlib.h>
/* 引入 string.h；标准字符串/内存处理头文件；memcpy、memset、strcmp 等基础接口都来自这里。 */
#include <string.h>
/* 引入 time.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <time.h>

/* 引入 sdkconfig.h；编译期配置头文件；menuconfig 里配置出的宏会在这里展开。 */
#include "sdkconfig.h"

/* 引入 freertos/FreeRTOS.h；FreeRTOS 核心头文件；任务、队列、事件组等内核对象的基础定义都依赖它。 */
#include "freertos/FreeRTOS.h"
/* 引入 freertos/event_groups.h；FreeRTOS 事件组头文件；多个标志位同步时比队列更合适。 */
#include "freertos/event_groups.h"
/* 引入 freertos/task.h；FreeRTOS 任务头文件；xTaskCreate、vTaskDelay、任务通知等接口主要在这里声明。 */
#include "freertos/task.h"

/* 引入 esp_check.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_check.h"
/* 引入 esp_crt_bundle.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_crt_bundle.h"
/* 引入 esp_err.h；ESP-IDF 错误码头文件；esp_err_t、ESP_OK、ESP_ERROR_CHECK 等错误处理机制依赖它。 */
#include "esp_err.h"
/* 引入 esp_event.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_event.h"
/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"
/* 引入 esp_netif.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_netif.h"
/* 引入 esp_wifi.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_wifi.h"
/* 引入 mqtt_client.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "mqtt_client.h"













/* 这里把 "app_cloud" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_cloud";
















































































































/* 定义宏 APP_CLOUD_WIFI_CONNECTED_BIT；这里把“应用、云端、Wi‑Fi、已连接、BIT”集中写成常量 BIT0，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_WIFI_CONNECTED_BIT   BIT0
/* 定义宏 APP_CLOUD_MQTT_CONNECTED_BIT；这里把“应用、云端、MQTT、已连接、BIT”集中写成常量 BIT1，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_MQTT_CONNECTED_BIT   BIT1
/* 定义宏 APP_CLOUD_START_MQTT_BIT；这里把“应用、云端、启动、MQTT、BIT”集中写成常量 BIT2，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_START_MQTT_BIT       BIT2

/* 定义宏 APP_CLOUD_TASK_STACK_SIZE；这里把“应用、云端、任务、STACK、大小”集中写成常量 (8 * 1024)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_TASK_STACK_SIZE      (8 * 1024)
/* 定义宏 APP_CLOUD_TASK_PRIORITY；这里把“应用、云端、任务、PRIORITY”集中写成常量 5，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_TASK_PRIORITY        5

/* 定义宏 APP_CLOUD_TOPIC_BUF_LEN；这里把“应用、云端、主题、缓冲区、长度”集中写成常量 192，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_TOPIC_BUF_LEN        192
/* 定义宏 APP_CLOUD_JSON_BUF_LEN；这里把“应用、云端、JSON、缓冲区、长度”集中写成常量 512，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_JSON_BUF_LEN         512
/* 定义宏 APP_CLOUD_CMD_PAYLOAD_LEN；这里把“应用、云端、CMD、载荷、长度”集中写成常量 256，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_CMD_PAYLOAD_LEN      256

/* 定义宏 APP_CLOUD_LOG_AUTH；这里把“应用、云端、LOG、AUTH”集中写成常量 1，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define APP_CLOUD_LOG_AUTH             1



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这里先定义变量 cmd，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char cmd[24];

    /* 这里先定义变量 target_id，类型是 uint16_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint16_t target_id;

    /* 这里先定义变量 request_id，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char request_id[32];

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_cloud_cmd_t;


/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这里先定义变量 inited，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool inited;

    /* 这里先定义变量 mqtt_started，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool mqtt_started;

    /* 这里先定义变量 mqtt_connected，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool mqtt_connected;

    /* 这里先定义变量 have_last_snapshot，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool have_last_snapshot;

    /* 这里先定义变量 msg_seq，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t msg_seq;

    /* 这里先定义变量 event_group，类型是 EventGroupHandle_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    EventGroupHandle_t event_group;

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    esp_netif_t *sta_netif;

    /* 这里先定义变量 mqtt_client，类型是 esp_mqtt_client_handle_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    esp_mqtt_client_handle_t mqtt_client;

    /* 这里先定义变量 last_snapshot，类型是 app_task_snapshot_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_task_snapshot_t last_snapshot;

    /* 这里先定义变量 current_request_id，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char current_request_id[32];

    /* 这里先定义变量 topic_cmd，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char topic_cmd[APP_CLOUD_TOPIC_BUF_LEN];

    /* 这里先定义变量 topic_ack，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char topic_ack[APP_CLOUD_TOPIC_BUF_LEN];

    /* 这里先定义变量 topic_state，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char topic_state[APP_CLOUD_TOPIC_BUF_LEN];

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_cloud_runtime_t;


/* 这里定义变量 s_cloud，类型是 static app_cloud_runtime_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_cloud_runtime_t s_cloud = {0};

/* 这里定义变量 s_cloud_task，类型是 static TaskHandle_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
static TaskHandle_t s_cloud_task = NULL;

/* 这里开始定义函数 app_cloud_mqtt_event_handler；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_mqtt_event_handler(void *handler_args,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         esp_event_base_t base,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         int32_t event_id,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         void *event_data);





/* 这里开始定义函数 app_cloud_json_get_string；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_cloud_json_get_string(const char *json,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      const char *key,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      char *out,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      size_t out_size)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 json == NULL || key == NULL || out == NULL || out_size == 0U；只有条件成立，后面的分支代码才会执行。 */
    if (json == NULL || key == NULL || out == NULL || out_size == 0U) {
        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里先定义变量 pattern，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char pattern[48];

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);


    /* 这里把 strstr(json, pattern) 写入 const、char、p；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    const char *p = strstr(json, pattern);

    /* 这里开始判断条件 p == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (p == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 strlen(pattern) 写入 p；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    p += strlen(pattern);

    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        p++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 *p != ':'；只有条件成立，后面的分支代码才会执行。 */
    if (*p != ':') {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    p++;


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        p++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 *p != '"'；只有条件成立，后面的分支代码才会执行。 */
    if (*p != '"') {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    p++;


    /* 这里定义变量 i，类型是 size_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    size_t i = 0;

    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (*p != '\0' && *p != '"' && i + 1U < out_size) {

        /* 这里开始判断条件 *p == '\\' && p[1] != '\0'；只有条件成立，后面的分支代码才会执行。 */
        if (*p == '\\' && p[1] != '\0') {

            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            p++;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里把 *p++ 写入 out、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        out[i++] = *p++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 *p != '"'；只有条件成立，后面的分支代码才会执行。 */
    if (*p != '"') {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 '\0' 写入 out、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out[i] = '\0';

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_json_get_u16；返回类型是 static bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static bool app_cloud_json_get_u16(const char *json,
                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                   const char *key,
                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                   uint16_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 json == NULL || key == NULL || out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (json == NULL || key == NULL || out == NULL) {
        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里先定义变量 pattern，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char pattern[48];

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);


    /* 这里把 strstr(json, pattern) 写入 const、char、p；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    const char *p = strstr(json, pattern);

    /* 这里开始判断条件 p == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (p == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 strlen(pattern) 写入 p；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    p += strlen(pattern);

    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        p++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 *p != ':'；只有条件成立，后面的分支代码才会执行。 */
    if (*p != ':') {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    p++;


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        p++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !isdigit((unsigned char)*p)；只有条件成立，后面的分支代码才会执行。 */
    if (!isdigit((unsigned char)*p)) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 value，类型是 unsigned，并且在声明时就把初值设成 0U；这样后面第一次使用它时就是一个确定状态。 */
    unsigned value = 0U;

    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (isdigit((unsigned char)*p)) {

        /* 这里把 value * 10U + (unsigned)(*p - '0') 写入 value；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        value = value * 10U + (unsigned)(*p - '0');

        /* 这里开始判断条件 value > UINT16_MAX；只有条件成立，后面的分支代码才会执行。 */
        if (value > UINT16_MAX) {

            /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return false;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        p++;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 (uint16_t)value 写入 out；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *out = (uint16_t)value;

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_parse_command_json；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_parse_command_json(const char *payload, app_cloud_cmd_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 payload == NULL || out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (payload == NULL || out == NULL) {

        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(out, 0, sizeof(*out));


    /* 这里开始判断条件 !app_cloud_json_get_string(payload, "cmd", out->cmd, sizeof(out->cmd))；只有条件成立，后面的分支代码才会执行。 */
    if (!app_cloud_json_get_string(payload, "cmd", out->cmd, sizeof(out->cmd))) {

        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_cloud_json_get_u16；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    (void)app_cloud_json_get_u16(payload, "target_id", &out->target_id);

    /* 调用本项目模块接口 app_cloud_json_get_string；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    (void)app_cloud_json_get_string(payload, "request_id", out->request_id, sizeof(out->request_id));

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_init_netif_once；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_init_netif_once(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 esp_netif_init()；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = esp_netif_init();

    /* 这里开始判断条件 ret == ESP_ERR_INVALID_STATE；只有条件成立，后面的分支代码才会执行。 */
    if (ret == ESP_ERR_INVALID_STATE) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ret;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_init_event_loop_once；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_init_event_loop_once(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 esp_event_loop_create_default()；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = esp_event_loop_create_default();

    /* 这里开始判断条件 ret == ESP_ERR_INVALID_STATE；只有条件成立，后面的分支代码才会执行。 */
    if (ret == ESP_ERR_INVALID_STATE) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ret;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_build_topics；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_build_topics(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(s_cloud.topic_cmd,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             sizeof(s_cloud.topic_cmd),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "%s/%s/cmd",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_TOPIC_PREFIX,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_DEVICE_NAME);


    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(s_cloud.topic_ack,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             sizeof(s_cloud.topic_ack),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "%s/%s/ack",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_TOPIC_PREFIX,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_DEVICE_NAME);


    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(s_cloud.topic_state,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             sizeof(s_cloud.topic_state),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "%s/%s/state",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_TOPIC_PREFIX,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_DEVICE_NAME);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_log_topics_once；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_log_topics_once(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "wifi ssid   : %s", CONFIG_SKY_WIFI_SSID);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "mqtt broker : %s", CONFIG_SKY_MQTT_BROKER_URI);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "client id   : %s", CONFIG_SKY_MQTT_CLIENT_ID);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "device name : %s", CONFIG_SKY_MQTT_DEVICE_NAME);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "topic cmd   : %s", s_cloud.topic_cmd);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "topic ack   : %s", s_cloud.topic_ack);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "topic state : %s", s_cloud.topic_state);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "cmd example : {\"cmd\":\"start_task\",\"target_id\":3,\"request_id\":\"abc001\"}");
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_validate_config；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_validate_config(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 CONFIG_SKY_WIFI_SSID[0] == '\0'；只有条件成立，后面的分支代码才会执行。 */
    if (CONFIG_SKY_WIFI_SSID[0] == '\0') {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "CONFIG_SKY_WIFI_SSID is empty, please set it in menuconfig");

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 CONFIG_SKY_MQTT_BROKER_URI[0] == '\0'；只有条件成立，后面的分支代码才会执行。 */
    if (CONFIG_SKY_MQTT_BROKER_URI[0] == '\0') {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_BROKER_URI is empty");

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 CONFIG_SKY_MQTT_CLIENT_ID[0] == '\0'；只有条件成立，后面的分支代码才会执行。 */
    if (CONFIG_SKY_MQTT_CLIENT_ID[0] == '\0') {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_CLIENT_ID is empty");

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 CONFIG_SKY_MQTT_DEVICE_NAME[0] == '\0'；只有条件成立，后面的分支代码才会执行。 */
    if (CONFIG_SKY_MQTT_DEVICE_NAME[0] == '\0') {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_DEVICE_NAME is empty");

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 CONFIG_SKY_MQTT_TOPIC_PREFIX[0] == '\0'；只有条件成立，后面的分支代码才会执行。 */
    if (CONFIG_SKY_MQTT_TOPIC_PREFIX[0] == '\0') {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "CONFIG_SKY_MQTT_TOPIC_PREFIX is empty");

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 strlen(CONFIG_SKY_WIFI_SSID) >= sizeof(((wifi_config_t *)0)->sta.ssid)；只有条件成立，后面的分支代码才会执行。 */
    if (strlen(CONFIG_SKY_WIFI_SSID) >= sizeof(((wifi_config_t *)0)->sta.ssid)) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "Wi-Fi SSID too long");

        /* 这里把 ESP_ERR_INVALID_SIZE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_SIZE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 strlen(CONFIG_SKY_WIFI_PASSWORD) >= sizeof(((wifi_config_t *)0)->sta.password)；只有条件成立，后面的分支代码才会执行。 */
    if (strlen(CONFIG_SKY_WIFI_PASSWORD) >= sizeof(((wifi_config_t *)0)->sta.password)) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "Wi-Fi password too long");

        /* 这里把 ESP_ERR_INVALID_SIZE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_SIZE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_cloud_publish_raw；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_publish_raw(const char *topic,
                                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                       const char *payload,
                                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                       int qos,
                                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                       int retain)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 topic == NULL || payload == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (topic == NULL || payload == NULL) {
        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_cloud.mqtt_client == NULL || !s_cloud.mqtt_connected；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.mqtt_client == NULL || !s_cloud.mqtt_connected) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 msg_id，类型是 int，并且在声明时就把初值设成 esp_mqtt_client_publish(s_cloud.mqtt_client, topic, payload, 0, qos, retain)；这样后面第一次使用它时就是一个确定状态。 */
    int msg_id = esp_mqtt_client_publish(s_cloud.mqtt_client, topic, payload, 0, qos, retain);

    /* 这里开始判断条件 msg_id < 0；只有条件成立，后面的分支代码才会执行。 */
    if (msg_id < 0) {

        /* 这里把 %s", topic) 写入 ESP、LOGW、标签、MQTT、publish、failed、主题；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ESP_LOGW(TAG, "mqtt publish failed, topic=%s", topic);

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 %s msg_id=%d payload=%s", topic, msg_id, payload) 写入 ESP、LOGI、标签、MQTT、发送、主题；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "mqtt tx topic=%s msg_id=%d payload=%s", topic, msg_id, payload);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_cloud_publish_ack；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_publish_ack(const app_cloud_cmd_t *cmd,
                                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                       int code,
                                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                       const char *msg,
                                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                       uint16_t target_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 cmd == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (cmd == NULL) {
        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里先定义变量 json，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char json[APP_CLOUD_JSON_BUF_LEN];

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(json,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             sizeof(json),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "{"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"request_id\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"cmd\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"code\":%d,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"msg\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"target_id\":%u"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "}",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             cmd->request_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             cmd->cmd,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             code,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (msg != NULL) ? msg : "-",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)target_id);


    /* 这里把 app_cloud_publish_raw(s_cloud.topic_ack, json, CONFIG_SKY_MQTT_QOS, 0) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return app_cloud_publish_raw(s_cloud.topic_ack, json, CONFIG_SKY_MQTT_QOS, 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_publish_task_snapshot_internal；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_publish_task_snapshot_internal(const app_task_snapshot_t *snap)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 snap == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (snap == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 seq，类型是 uint32_t，并且在声明时就把初值设成 ++s_cloud.msg_seq；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t seq = ++s_cloud.msg_seq;

    /* 这里定义变量 now，类型是 time_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    time_t now = 0;

    /* 调用函数 time；从名字看，它承担的职责和“time”有关，后续行为取决于这个接口的返回结果或副作用。 */
    time(&now);


    /* 这里定义变量 cargo_received，类型是 int，并且在声明时就把初值设成 (snap->state == APP_TASK_STATE_COMPLETED) ? 1 : 0；这样后面第一次使用它时就是一个确定状态。 */
    int cargo_received = (snap->state == APP_TASK_STATE_COMPLETED) ? 1 : 0;

    /* 这里定义变量 fault，类型是 int，并且在声明时就把初值设成 (snap->state == APP_TASK_STATE_FAULT) ? 1 : 0；这样后面第一次使用它时就是一个确定状态。 */
    int fault = (snap->state == APP_TASK_STATE_FAULT) ? 1 : 0;


    /* 这里先定义变量 json，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char json[APP_CLOUD_JSON_BUF_LEN];

    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(json,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             sizeof(json),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "{"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"msg_id\":%u,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"ts\":%lld,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"request_id\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"device\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"state\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"active\":%u,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"target_id\":%u,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"matched_tag_id\":%u,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"cargo_received\":%d,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"fault\":%d,"
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"source\":\"%s\","
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "\"note\":\"%s\""
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "}",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)seq,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (long long)now,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             s_cloud.current_request_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             CONFIG_SKY_MQTT_DEVICE_NAME,
             /* 调用本项目模块接口 app_task_state_to_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
             app_task_state_to_text(snap->state),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             snap->active ? 1U : 0U,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)snap->target_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)snap->matched_tag_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             cargo_received,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             fault,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             snap->source,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             snap->note);


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_cloud_publish_raw(s_cloud.topic_state, json, CONFIG_SKY_MQTT_QOS, 1)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = app_cloud_publish_raw(s_cloud.topic_state, json, CONFIG_SKY_MQTT_QOS, 1);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
        ESP_LOGW(TAG, "task snapshot not sent yet: %s", esp_err_to_name(ret));
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_on_task_event；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_on_task_event(app_task_event_t event,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    const app_task_snapshot_t *snap,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    void *user_ctx)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)event;

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)user_ctx;


    /* 这里开始判断条件 snap == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (snap == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 *snap 写入 s、云端、last、snapshot；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.last_snapshot = *snap;

    /* 这里把 true 写入 s、云端、have、last、snapshot；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.have_last_snapshot = true;

    /* 调用本项目模块接口 app_cloud_publish_task_snapshot_internal；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_cloud_publish_task_snapshot_internal(snap);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_create_mqtt_client；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_create_mqtt_client(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_cloud.mqtt_client != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.mqtt_client != NULL) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里定义变量 mqtt_cfg，类型是 esp_mqtt_client_config_t，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
    esp_mqtt_client_config_t mqtt_cfg = {
        /* 这里把 CONFIG_SKY_MQTT_BROKER_URI, 写入 代理、address、uri；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .broker.address.uri = CONFIG_SKY_MQTT_BROKER_URI,
        /* 这里把 CONFIG_SKY_MQTT_USERNAME, 写入 credentials、username；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .credentials.username = CONFIG_SKY_MQTT_USERNAME,
        /* 这里把 CONFIG_SKY_MQTT_CLIENT_ID, 写入 credentials、客户端、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .credentials.client_id = CONFIG_SKY_MQTT_CLIENT_ID,
        /* 这里把 CONFIG_SKY_MQTT_PASSWORD, 写入 credentials、authentication、password；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .credentials.authentication.password = CONFIG_SKY_MQTT_PASSWORD,
        /* 这里把 CONFIG_SKY_MQTT_KEEPALIVE_SEC, 写入 session、keepalive；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .session.keepalive = CONFIG_SKY_MQTT_KEEPALIVE_SEC,
        /* 这里把 5000, 写入 network、reconnect、超时、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .network.reconnect_timeout_ms = 5000,
        /* 这里把 10000, 写入 network、超时、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        .network.timeout_ms = 10000,
    /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
    };

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if CONFIG_SKY_MQTT_USE_CERT_BUNDLE

    /* 这里把 esp_crt_bundle_attach 写入 MQTT、配置、代理、verification、crt、bundle、attach；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
/* 切换到新的条件编译分支；用来在不同芯片能力、不同配置之间选择不同实现。 */
#elif CONFIG_SKY_MQTT_SKIP_SERVER_CERT_VERIFY

    /* 这里把 true 写入 MQTT、配置、代理、verification、skip、cert、common、名称、check；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if APP_CLOUD_LOG_AUTH

    /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
    ESP_LOGW(TAG, "==== EMQX mqtt auth debug begin ====");

    /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
    ESP_LOGW(TAG, "debug mqtt username : %s", mqtt_cfg.credentials.username);

    /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
    ESP_LOGW(TAG, "debug mqtt client_id: %s", mqtt_cfg.credentials.client_id);

    /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
    ESP_LOGW(TAG, "debug mqtt password : %s", mqtt_cfg.credentials.authentication.password);

    /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
    ESP_LOGW(TAG, "==== EMQX mqtt auth debug end ====");
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif


    /* 这里把 esp_mqtt_client_init(&mqtt_cfg) 写入 s、云端、MQTT、客户端；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    /* 这里开始判断条件 s_cloud.mqtt_client == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.mqtt_client == NULL) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_destroy_mqtt_client；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_destroy_mqtt_client(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_cloud.mqtt_client != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.mqtt_client != NULL) {

        /* 调用 ESP-IDF 提供的 esp_mqtt_client_destroy 接口；这类函数通常在操作芯片底层资源、驱动或系统服务。 */
        esp_mqtt_client_destroy(s_cloud.mqtt_client);

        /* 这里把 NULL 写入 s、云端、MQTT、客户端；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_cloud.mqtt_client = NULL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 false 写入 s、云端、MQTT、started；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.mqtt_started = false;

    /* 这里把 false 写入 s、云端、MQTT、已连接；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.mqtt_connected = false;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_start_mqtt_if_needed；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_start_mqtt_if_needed(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_cloud.mqtt_client == NULL && app_cloud_create_mqtt_client() != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.mqtt_client == NULL && app_cloud_create_mqtt_client() != ESP_OK) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_cloud.mqtt_client == NULL || s_cloud.mqtt_started；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.mqtt_client == NULL || s_cloud.mqtt_started) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_cloud.mqtt_client,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   ESP_EVENT_ANY_ID,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   app_cloud_mqtt_event_handler,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   NULL));

    /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_cloud.mqtt_client));

    /* 这里把 true 写入 s、云端、MQTT、started；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.mqtt_started = true;

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "EMQX mqtt client started");
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_receive_set_target；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_receive_set_target(uint16_t target_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{


    /* 这里把 %u", (unsigned)target_id) 写入 ESP、LOGI、标签、云端、接收、设置、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "cloud rx: set_target=%u", (unsigned)target_id);

    /* 这里把 app_task_set_target_id(target_id, true) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return app_task_set_target_id(target_id, true);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_receive_start_task；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_receive_start_task(const app_cloud_cmd_t *cmd)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 cmd == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (cmd == NULL) {
        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里先定义变量 previous_request_id，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char previous_request_id[sizeof(s_cloud.current_request_id)];
    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(previous_request_id, s_cloud.current_request_id, sizeof(previous_request_id));
    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(s_cloud.current_request_id, cmd->request_id, sizeof(s_cloud.current_request_id));


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG,
             /* 这里把 %u request_id=%s", 写入 云端、接收、启动、任务、目标；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
             "cloud rx: start_task target=%u request_id=%s",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)cmd->target_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (cmd->request_id[0] != '\0') ? cmd->request_id : "-");

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_task_submit_remote_request(cmd->target_id, "emqx")；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = app_task_submit_remote_request(cmd->target_id, "emqx");
    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
        strlcpy(s_cloud.current_request_id, previous_request_id, sizeof(s_cloud.current_request_id));
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ret;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_receive_cancel；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_receive_cancel(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "cloud rx: cancel");

    /* 调用本项目模块接口 app_task_cancel；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_cancel("cancelled by cloud");

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_handle_command；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_cloud_handle_command(const char *payload, size_t payload_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里先定义变量 json，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char json[APP_CLOUD_CMD_PAYLOAD_LEN];


    /* 这里开始判断条件 payload == NULL || payload_len == 0U；只有条件成立，后面的分支代码才会执行。 */
    if (payload == NULL || payload_len == 0U) {

        /* 这里把 ESP_ERR_INVALID_ARG 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_ARG;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 copy_len，类型是 size_t，并且在声明时就把初值设成 payload_len；这样后面第一次使用它时就是一个确定状态。 */
    size_t copy_len = payload_len;

    /* 这里开始判断条件 copy_len >= sizeof(json)；只有条件成立，后面的分支代码才会执行。 */
    if (copy_len >= sizeof(json)) {

        /* 这里把 sizeof(json) - 1U 写入 copy、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        copy_len = sizeof(json) - 1U;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
    memcpy(json, payload, copy_len);

    /* 这里把 '\0' 写入 JSON、copy、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    json[copy_len] = '\0';


    /* 这里定义变量 cmd，类型是 app_cloud_cmd_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    app_cloud_cmd_t cmd = {0};

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_cloud_parse_command_json(json, &cmd)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = app_cloud_parse_command_json(json, &cmd);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
        ESP_LOGW(TAG, "bad EMQX cmd payload: %s", json);

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG,
             /* 这里把 %s target=%u request_id=%s", 写入 EMQX、cmd、接收、cmd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
             "EMQX cmd rx cmd=%s target=%u request_id=%s",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             cmd.cmd,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)cmd.target_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (cmd.request_id[0] != '\0') ? cmd.request_id : "-");


    /* 这里开始判断条件 strcmp(cmd.cmd, "start_task") == 0；只有条件成立，后面的分支代码才会执行。 */
    if (strcmp(cmd.cmd, "start_task") == 0) {

        /* 这里把 app_cloud_receive_start_task(&cmd) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ret = app_cloud_receive_start_task(&cmd);

        /* 调用本项目模块接口 app_cloud_publish_ack；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_cloud_publish_ack(&cmd,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    (ret == ESP_OK) ? 0 : -1,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    (ret == ESP_OK) ? "accepted" : "start_failed",
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    cmd.target_id);

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 strcmp(cmd.cmd, "set_target") == 0；只有条件成立，后面的分支代码才会执行。 */
    if (strcmp(cmd.cmd, "set_target") == 0) {

        /* 这里把 app_cloud_receive_set_target(cmd.target_id) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ret = app_cloud_receive_set_target(cmd.target_id);

        /* 调用本项目模块接口 app_cloud_publish_ack；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_cloud_publish_ack(&cmd,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    (ret == ESP_OK) ? 0 : -1,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    (ret == ESP_OK) ? "accepted" : "set_failed",
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    cmd.target_id);

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 strcmp(cmd.cmd, "cancel") == 0；只有条件成立，后面的分支代码才会执行。 */
    if (strcmp(cmd.cmd, "cancel") == 0) {

        /* 这里把 app_cloud_receive_cancel() 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ret = app_cloud_receive_cancel();

        /* 调用本项目模块接口 app_cloud_publish_ack；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_cloud_publish_ack(&cmd,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    (ret == ESP_OK) ? 0 : -1,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    (ret == ESP_OK) ? "accepted" : "cancel_failed",
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    0U);

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 %s", cmd.cmd) 写入 ESP、LOGW、标签、unknown、cmd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGW(TAG, "unknown cmd=%s", cmd.cmd);

    /* 调用本项目模块接口 app_cloud_publish_ack；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    (void)app_cloud_publish_ack(&cmd, -2, "unknown_cmd", cmd.target_id);

    /* 这里把 ESP_ERR_NOT_SUPPORTED 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_ERR_NOT_SUPPORTED;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_handle_mqtt_data_event；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_handle_mqtt_data_event(esp_mqtt_event_handle_t event)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 event == NULL || event->topic == NULL || event->data == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (event == NULL || event->topic == NULL || event->data == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里先定义变量 topic，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char topic[APP_CLOUD_TOPIC_BUF_LEN];

    /* 这里先定义变量 payload，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char payload[APP_CLOUD_JSON_BUF_LEN];

    /* 这里定义变量 topic_len，类型是 int，并且在声明时就把初值设成 event->topic_len；这样后面第一次使用它时就是一个确定状态。 */
    int topic_len = event->topic_len;

    /* 这里定义变量 data_len，类型是 int，并且在声明时就把初值设成 event->data_len；这样后面第一次使用它时就是一个确定状态。 */
    int data_len = event->data_len;


    /* 这里开始判断条件 topic_len <= 0；只有条件成立，后面的分支代码才会执行。 */
    if (topic_len <= 0) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 (size_t)topic_len >= sizeof(topic)；只有条件成立，后面的分支代码才会执行。 */
    if ((size_t)topic_len >= sizeof(topic)) {

        /* 这里把 sizeof(topic) - 1 写入 主题、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        topic_len = sizeof(topic) - 1;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
    memcpy(topic, event->topic, (size_t)topic_len);

    /* 这里把 '\0' 写入 主题、主题、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    topic[topic_len] = '\0';


    /* 这里开始判断条件 data_len < 0；只有条件成立，后面的分支代码才会执行。 */
    if (data_len < 0) {

        /* 这里把 0 写入 data、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        data_len = 0;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 (size_t)data_len >= sizeof(payload)；只有条件成立，后面的分支代码才会执行。 */
    if ((size_t)data_len >= sizeof(payload)) {

        /* 这里把 sizeof(payload) - 1 写入 data、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        data_len = sizeof(payload) - 1;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 data_len > 0；只有条件成立，后面的分支代码才会执行。 */
    if (data_len > 0) {

        /* 把源地址的一段连续内存复制到目标地址；处理固定长度的图像、协议包或结构体快照时很常见。 */
        memcpy(payload, event->data, (size_t)data_len);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 '\0' 写入 载荷、data、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    payload[data_len] = '\0';


    /* 这里把 %s payload=%s", topic, payload) 写入 ESP、LOGI、标签、MQTT、接收、主题；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "mqtt rx topic=%s payload=%s", topic, payload);


    /* 这里开始判断条件 strcmp(topic, s_cloud.topic_cmd) == 0；只有条件成立，后面的分支代码才会执行。 */
    if (strcmp(topic, s_cloud.topic_cmd) == 0) {

        /* 调用本项目模块接口 app_cloud_handle_command；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        (void)app_cloud_handle_command(payload, (size_t)data_len);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_task；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_task(void *arg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)arg;


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (1) {

        /* 这里定义变量 bits，类型是 EventBits_t，并且在声明时就把初值设成 xEventGroupWaitBits(s_cloud.event_group,；这样后面第一次使用它时就是一个确定状态。 */
        EventBits_t bits = xEventGroupWaitBits(s_cloud.event_group,
                                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                               APP_CLOUD_START_MQTT_BIT,
                                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                               pdTRUE,
                                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                               pdFALSE,
                                               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                               portMAX_DELAY);


        /* 这里开始判断条件 (bits & APP_CLOUD_START_MQTT_BIT) != 0；只有条件成立，后面的分支代码才会执行。 */
        if ((bits & APP_CLOUD_START_MQTT_BIT) != 0) {

            /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
            ESP_LOGI(TAG, "cloud task: start mqtt");

            /* 调用本项目模块接口 app_cloud_start_mqtt_if_needed；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_cloud_start_mqtt_if_needed();
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_cloud_wifi_event_handler；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_wifi_event_handler(void *arg,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         esp_event_base_t event_base,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         int32_t event_id,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         void *event_data)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)arg;


    /* 这里开始判断条件 event_base == WIFI_EVENT；只有条件成立，后面的分支代码才会执行。 */
    if (event_base == WIFI_EVENT) {

        /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
        switch (event_id) {

            /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
            case WIFI_EVENT_STA_START:

                /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
                ESP_LOGI(TAG, "wifi sta start -> connect");

                /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
                ESP_ERROR_CHECK(esp_wifi_connect());

                /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
                break;

            /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
            case WIFI_EVENT_STA_DISCONNECTED: {

                /* 这里把 (wifi_event_sta_disconnected_t *)event_data 写入 Wi‑Fi、事件、sta、disconnected、t、disc；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;

                /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
                ESP_LOGW(TAG,
                         /* 这里把 %d", 写入 Wi‑Fi、disconnected、reconnect、reason；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                         "wifi disconnected -> reconnect, reason=%d",
                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                         disc ? disc->reason : -1);

                /* 调用函数 xEventGroupClearBits；从名字看，它承担的职责和“xEventGroupClearBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
                xEventGroupClearBits(s_cloud.event_group,
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     APP_CLOUD_WIFI_CONNECTED_BIT |
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     APP_CLOUD_MQTT_CONNECTED_BIT |
                                     /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                     APP_CLOUD_START_MQTT_BIT);

                /* 这里把 false 写入 s、云端、MQTT、已连接；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                s_cloud.mqtt_connected = false;

                /* 调用本项目模块接口 app_cloud_destroy_mqtt_client；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_cloud_destroy_mqtt_client();

                /* 调用 ESP-IDF 的强校验宏；如果括号里的接口返回失败，系统会立刻打印错误并中止继续执行，方便尽早暴露初始化问题。 */
                ESP_ERROR_CHECK(esp_wifi_connect());

                /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
                break;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }


            /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
            default:

                /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
                break;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
        ESP_LOGI(TAG, "wifi got ip");

        /* 调用函数 xEventGroupSetBits；从名字看，它承担的职责和“xEventGroupSetBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
        xEventGroupSetBits(s_cloud.event_group,
                           /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                           APP_CLOUD_WIFI_CONNECTED_BIT | APP_CLOUD_START_MQTT_BIT);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_cloud_mqtt_event_handler；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_cloud_mqtt_event_handler(void *handler_args,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         esp_event_base_t base,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         int32_t event_id,
                                         /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                         void *event_data)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)handler_args;

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)base;


    /* 这里定义变量 event，类型是 esp_mqtt_event_handle_t，并且在声明时就把初值设成 (esp_mqtt_event_handle_t)event_data；这样后面第一次使用它时就是一个确定状态。 */
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;


    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch ((esp_mqtt_event_id_t)event_id) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case MQTT_EVENT_CONNECTED:

            /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
            ESP_LOGI(TAG, "EMQX mqtt connected");

            /* 这里把 true 写入 s、云端、MQTT、已连接；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_cloud.mqtt_connected = true;

            /* 调用函数 xEventGroupSetBits；从名字看，它承担的职责和“xEventGroupSetBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
            xEventGroupSetBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);
            /* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
            {

                /* 这里定义变量 sub_cmd，类型是 int，并且在声明时就把初值设成 esp_mqtt_client_subscribe(s_cloud.mqtt_client,；这样后面第一次使用它时就是一个确定状态。 */
                int sub_cmd = esp_mqtt_client_subscribe(s_cloud.mqtt_client,
                                                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                        s_cloud.topic_cmd,
                                                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                        CONFIG_SKY_MQTT_QOS);

                /* 这里把 %d", sub_cmd) 写入 ESP、LOGI、标签、subscribe、sent、cmd；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                ESP_LOGI(TAG, "subscribe sent cmd=%d", sub_cmd);
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里开始判断条件 s_cloud.have_last_snapshot；只有条件成立，后面的分支代码才会执行。 */
            if (s_cloud.have_last_snapshot) {

                /* 调用本项目模块接口 app_cloud_publish_task_snapshot_internal；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
                app_cloud_publish_task_snapshot_internal(&s_cloud.last_snapshot);
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case MQTT_EVENT_DISCONNECTED:

            /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
            ESP_LOGW(TAG, "EMQX mqtt disconnected");

            /* 这里把 false 写入 s、云端、MQTT、已连接；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_cloud.mqtt_connected = false;

            /* 调用函数 xEventGroupClearBits；从名字看，它承担的职责和“xEventGroupClearBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
            xEventGroupClearBits(s_cloud.event_group, APP_CLOUD_MQTT_CONNECTED_BIT);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case MQTT_EVENT_SUBSCRIBED:

            /* 这里把 %d", event->msg_id) 写入 ESP、LOGI、标签、MQTT、subscribed、消息、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            ESP_LOGI(TAG, "mqtt subscribed, msg_id=%d", event->msg_id);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case MQTT_EVENT_PUBLISHED:

            /* 这里把 %d", event->msg_id) 写入 ESP、LOGI、标签、MQTT、published、消息、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            ESP_LOGI(TAG, "mqtt published, msg_id=%d", event->msg_id);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case MQTT_EVENT_DATA:

            /* 调用本项目模块接口 app_cloud_handle_mqtt_data_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_cloud_handle_mqtt_data_event(event);

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case MQTT_EVENT_ERROR:

            /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
            ESP_LOGW(TAG, "mqtt error event");

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;


        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_init；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_cloud_init(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_cloud.inited；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.inited) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(app_cloud_validate_config(), TAG, "invalid network/mqtt config");


    /* 这里把 xEventGroupCreate() 写入 s、云端、事件、组；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.event_group = xEventGroupCreate();

    /* 这里开始判断条件 s_cloud.event_group == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.event_group == NULL) {

        /* 这里把 ESP_ERR_NO_MEM 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_NO_MEM;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_cloud_build_topics；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_cloud_build_topics();


    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(app_task_register_event_callback(app_cloud_on_task_event, NULL),
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        TAG,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        "register task callback failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(app_cloud_init_netif_once(), TAG, "esp_netif_init failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(app_cloud_init_event_loop_once(), TAG, "event loop init failed");


    /* 这里把 esp_netif_create_default_wifi_sta() 写入 s、云端、sta、netif；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.sta_netif = esp_netif_create_default_wifi_sta();

    /* 这里开始判断条件 s_cloud.sta_netif == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud.sta_netif == NULL) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 wifi_init_cfg，类型是 wifi_init_config_t，并且在声明时就把初值设成 WIFI_INIT_CONFIG_DEFAULT()；这样后面第一次使用它时就是一个确定状态。 */
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "esp_wifi_init failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   ESP_EVENT_ANY_ID,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   &app_cloud_wifi_event_handler,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   NULL),
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        TAG,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        "register wifi event failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   IP_EVENT_STA_GOT_IP,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   &app_cloud_wifi_event_handler,
                                                   /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                                   NULL),
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        TAG,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        "register ip event failed");


    /* 这里开始判断条件 s_cloud_task == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cloud_task == NULL) {

        /* 这里定义变量 ok，类型是 BaseType_t，并且在声明时就把初值设成 xTaskCreate(app_cloud_task,；这样后面第一次使用它时就是一个确定状态。 */
        BaseType_t ok = xTaskCreate(app_cloud_task,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    "app_cloud",
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    APP_CLOUD_TASK_STACK_SIZE,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    NULL,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    APP_CLOUD_TASK_PRIORITY,
                                    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                    &s_cloud_task);

        /* 这里开始判断条件 ok != pdPASS；只有条件成立，后面的分支代码才会执行。 */
        if (ok != pdPASS) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "create app_cloud task failed");

            /* 这里把 ESP_ERR_NO_MEM 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ESP_ERR_NO_MEM;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 wifi_cfg，类型是 wifi_config_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    wifi_config_t wifi_cfg = {0};

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy((char *)wifi_cfg.sta.ssid,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            CONFIG_SKY_WIFI_SSID,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            sizeof(wifi_cfg.sta.ssid));

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy((char *)wifi_cfg.sta.password,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            CONFIG_SKY_WIFI_PASSWORD,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            sizeof(wifi_cfg.sta.password));

    /* 这里把 WIFI_FAST_SCAN 写入 Wi‑Fi、配置、sta、scan、method；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    wifi_cfg.sta.scan_method = WIFI_FAST_SCAN;

    /* 这里把 WIFI_CONNECT_AP_BY_SIGNAL 写入 Wi‑Fi、配置、sta、sort、method；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    /* 这里把 CONFIG_SKY_WIFI_MAXIMUM_RETRY 写入 Wi‑Fi、配置、sta、failure、retry、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    wifi_cfg.sta.failure_retry_cnt = CONFIG_SKY_WIFI_MAXIMUM_RETRY;


    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi set storage failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi set mode failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "wifi set config failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    /* 调用函数 ESP_RETURN_ON_ERROR；从名字看，它承担的职责和“ESP、RETURN、ON、ERROR”有关，后续行为取决于这个接口的返回结果或副作用。 */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi set ps failed");


    /* 调用本项目模块接口 app_cloud_log_topics_once；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_cloud_log_topics_once();

    /* 这里把 true 写入 s、云端、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.inited = true;

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "EMQX init done (official host Wi-Fi path via ESP32-C6)");

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_cloud_publish_task_snapshot；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_cloud_publish_task_snapshot(const app_task_snapshot_t *snap)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 snap == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (snap == NULL) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 *snap 写入 s、云端、last、snapshot；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.last_snapshot = *snap;

    /* 这里把 true 写入 s、云端、have、last、snapshot；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_cloud.have_last_snapshot = true;

    /* 调用本项目模块接口 app_cloud_publish_task_snapshot_internal；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_cloud_publish_task_snapshot_internal(snap);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
