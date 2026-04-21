/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */



















































/* 引入本项目的 app_task 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_task.h"

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
/* 引入 nvs.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "nvs.h"














/* 这里把 "app_task" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_task";

/* 这里把 "sky_task" 写入 static、const、char、NVS、NS；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *NVS_NS = "sky_task";

/* 这里把 "target_id" 写入 static、const、char、NVS、KEY、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *NVS_KEY_TARGET_ID = "target_id";



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这里先定义变量 inited，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool inited;

    /* 这里先定义变量 active，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool active;

    /* 这里先定义变量 target_dirty，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool target_dirty;

    /* 这里先定义变量 target_id，类型是 uint16_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint16_t target_id;

    /* 这里先定义变量 matched_tag_id，类型是 uint16_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint16_t matched_tag_id;

    /* 这里先定义变量 state，类型是 app_task_state_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_task_state_t state;

    /* 这里先定义变量 source，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char source[20];

    /* 这里先定义变量 note，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
    char note[64];

    /* 这里先定义变量 state_since_ms，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t state_since_ms;

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_task_runtime_t;


/* 这里定义变量 s_mux，类型是 static portMUX_TYPE，并且在声明时就把初值设成 portMUX_INITIALIZER_UNLOCKED；这样后面第一次使用它时就是一个确定状态。 */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* 这里定义变量 s_rt，类型是 static app_task_runtime_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
static app_task_runtime_t s_rt = {0};

/* 这里定义变量 s_event_cb，类型是 static app_task_event_cb_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
static app_task_event_cb_t s_event_cb = NULL;

/* 这里把 NULL 写入 static、void、s、事件、user、上下文；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static void *s_event_user_ctx = NULL;






/* 这里开始定义函数 app_task_now_ms；返回类型是 static uint32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static uint32_t app_task_now_ms(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_change_state_locked；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_task_change_state_locked(app_task_state_t state, const char *note)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 state 写入 s、rt、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.state = state;

    /* 这里把 app_task_now_ms() 写入 s、rt、状态、since、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.state_since_ms = app_task_now_ms();

    /* 这里开始判断条件 note != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (note != NULL) {

        /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
        strlcpy(s_rt.note, note, sizeof(s_rt.note));
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 这里把 '\0' 写入 s、rt、note、0；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.note[0] = '\0';
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_emit_event；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_task_emit_event(app_task_event_t event)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 cb，类型是 app_task_event_cb_t，并且在声明时就把初值设成 NULL；这样后面第一次使用它时就是一个确定状态。 */
    app_task_event_cb_t cb = NULL;

    /* 这里把 NULL 写入 void、user、上下文；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    void *user_ctx = NULL;

    /* 这里定义变量 snap，类型是 app_task_snapshot_t，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    app_task_snapshot_t snap = {0};


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 s_event_cb 写入 cb；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    cb = s_event_cb;

    /* 这里把 s_event_user_ctx 写入 user、上下文；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    user_ctx = s_event_user_ctx;

    /* 这里把 s_rt.inited 写入 snap、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.inited = s_rt.inited;

    /* 这里把 s_rt.active 写入 snap、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.active = s_rt.active;

    /* 这里把 s_rt.target_dirty 写入 snap、目标、dirty；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.target_dirty = s_rt.target_dirty;

    /* 这里把 s_rt.target_id 写入 snap、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.target_id = s_rt.target_id;

    /* 这里把 s_rt.matched_tag_id 写入 snap、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.matched_tag_id = s_rt.matched_tag_id;

    /* 这里把 s_rt.state 写入 snap、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.state = s_rt.state;

    /* 这里把 s_rt.state_since_ms 写入 snap、状态、since、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    snap.state_since_ms = s_rt.state_since_ms;

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(snap.source, s_rt.source, sizeof(snap.source));

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(snap.note, s_rt.note, sizeof(snap.note));

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);


    /* 这里开始判断条件 cb != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (cb != NULL) {

        /* 调用函数 cb；从名字看，它承担的职责和“cb”有关，后续行为取决于这个接口的返回结果或副作用。 */
        cb(event, &snap, user_ctx);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_persist_target_id；返回类型是 static esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static esp_err_t app_task_persist_target_id(uint16_t target_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里先定义变量 handle，类型是 nvs_handle_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    nvs_handle_t handle;

    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 nvs_open(NVS_NS, NVS_READWRITE, &handle)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &handle);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ret;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 nvs_set_u32(handle, NVS_KEY_TARGET_ID, (uint32_t)target_id) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = nvs_set_u32(handle, NVS_KEY_TARGET_ID, (uint32_t)target_id);

    /* 这里开始判断条件 ret == ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret == ESP_OK) {

        /* 这里把 nvs_commit(handle) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ret = nvs_commit(handle);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 调用 NVS 相关接口 nvs_close；这类函数负责掉电保存配置或读取历史参数。 */
    nvs_close(handle);

    /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ret;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_load_target_id；返回类型是 static uint16_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static uint16_t app_task_load_target_id(uint16_t default_target_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里先定义变量 handle，类型是 nvs_handle_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    nvs_handle_t handle;

    /* 这里定义变量 value，类型是 uint32_t，并且在声明时就把初值设成 (uint32_t)default_target_id；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t value = (uint32_t)default_target_id;


    /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 nvs_open(NVS_NS, NVS_READONLY, &handle)；这样后面第一次使用它时就是一个确定状态。 */
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &handle);

    /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK) {

        /* 这里把 default_target_id 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return default_target_id;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 nvs_get_u32(handle, NVS_KEY_TARGET_ID, &value) 写入 ret；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ret = nvs_get_u32(handle, NVS_KEY_TARGET_ID, &value);

    /* 调用 NVS 相关接口 nvs_close；这类函数负责掉电保存配置或读取历史参数。 */
    nvs_close(handle);

    /* 这里开始判断条件 ret != ESP_OK || value > UINT16_MAX；只有条件成立，后面的分支代码才会执行。 */
    if (ret != ESP_OK || value > UINT16_MAX) {

        /* 这里把 default_target_id 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return default_target_id;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 (uint16_t)value 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint16_t)value;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_register_event_callback；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_task_register_event_callback(app_task_event_cb_t cb, void *user_ctx)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 cb 写入 s、事件、cb；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_event_cb = cb;

    /* 这里把 user_ctx 写入 s、事件、user、上下文；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_event_user_ctx = user_ctx;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_init；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_task_init(uint16_t default_target_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里开始判断条件 s_rt.inited；只有条件成立，后面的分支代码才会执行。 */
    if (s_rt.inited) {

        /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
        taskEXIT_CRITICAL(&s_mux);

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);


    /* 这里定义变量 loaded_target，类型是 uint16_t，并且在声明时就把初值设成 app_task_load_target_id(default_target_id)；这样后面第一次使用它时就是一个确定状态。 */
    uint16_t loaded_target = app_task_load_target_id(default_target_id);


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_rt, 0, sizeof(s_rt));

    /* 这里把 true 写入 s、rt、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.inited = true;

    /* 这里把 loaded_target 写入 s、rt、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.target_id = loaded_target;

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(s_rt.source, "local", sizeof(s_rt.source));

    /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "configured");

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);


    /* 这里把 %u", (unsigned)loaded_target) 写入 ESP、LOGI、标签、任务、初始化、done、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    ESP_LOGI(TAG, "task init done, target_id=%u", (unsigned)loaded_target);

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_INIT);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_get_target_id；返回类型是 uint16_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
uint16_t app_task_get_target_id(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 value，类型是 uint16_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint16_t value = 0;

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 s_rt.target_id 写入 value；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    value = s_rt.target_id;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 这里把 value 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return value;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_set_target_id；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_task_set_target_id(uint16_t target_id, bool persist)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_rt.inited；只有条件成立，后面的分支代码才会执行。 */
    if (!s_rt.inited) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 target_id 写入 s、rt、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.target_id = target_id;

    /* 这里把 0 写入 s、rt、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.matched_tag_id = 0;

    /* 这里把 true 写入 s、rt、目标、dirty；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.target_dirty = true;

    /* 这里开始判断条件 !s_rt.active；只有条件成立，后面的分支代码才会执行。 */
    if (!s_rt.active) {

        /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "target updated");
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);


    /* 这里开始判断条件 persist；只有条件成立，后面的分支代码才会执行。 */
    if (persist) {

        /* 这里定义变量 ret，类型是 esp_err_t，并且在声明时就把初值设成 app_task_persist_target_id(target_id)；这样后面第一次使用它时就是一个确定状态。 */
        esp_err_t ret = app_task_persist_target_id(target_id);

        /* 这里开始判断条件 ret != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
        if (ret != ESP_OK) {

            /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
            ESP_LOGW(TAG, "persist target id failed: %s", esp_err_to_name(ret));

            /* 这里把 ret 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ret;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "target_id set to %u", (unsigned)target_id);

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_TARGET_UPDATED);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_start_with_target；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_task_start_with_target(uint16_t target_id, const char *source)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !s_rt.inited；只有条件成立，后面的分支代码才会执行。 */
    if (!s_rt.inited) {

        /* 这里把 ESP_ERR_INVALID_STATE 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_ERR_INVALID_STATE;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 target_id 写入 s、rt、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.target_id = target_id;

    /* 这里把 true 写入 s、rt、目标、dirty；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.target_dirty = true;

    /* 这里把 true 写入 s、rt、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.active = true;

    /* 这里把 0 写入 s、rt、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.matched_tag_id = 0;

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(s_rt.source, (source != NULL) ? source : "local", sizeof(s_rt.source));

    /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_change_state_locked(APP_TASK_STATE_WAIT_APPROACH, "waiting target approach");

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "task started, target_id=%u source=%s", (unsigned)target_id, (source != NULL) ? source : "local");

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_start_local；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_task_start_local(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 app_task_start_with_target(app_task_get_target_id(), "touch") 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return app_task_start_with_target(app_task_get_target_id(), "touch");
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_submit_remote_request；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_task_submit_remote_request(uint16_t target_id, const char *source)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 app_task_start_with_target(target_id, (source != NULL) ? source : "remote") 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return app_task_start_with_target(target_id, (source != NULL) ? source : "remote");
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_mark_auth_passed；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_mark_auth_passed(uint16_t matched_tag_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 changed，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool changed = false;

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里开始判断条件 s_rt.active && s_rt.state == APP_TASK_STATE_WAIT_APPROACH；只有条件成立，后面的分支代码才会执行。 */
    if (s_rt.active && s_rt.state == APP_TASK_STATE_WAIT_APPROACH) {

        /* 这里把 matched_tag_id 写入 s、rt、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_rt.matched_tag_id = matched_tag_id;

        /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_change_state_locked(APP_TASK_STATE_AUTH_PASSED, "auth passed");

        /* 这里把 true 写入 changed；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        changed = true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 这里开始判断条件 changed；只有条件成立，后面的分支代码才会执行。 */
    if (changed) {

        /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_mark_docking_started；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_mark_docking_started(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 changed，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool changed = false;

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里开始判断条件 ...；只有条件成立，后面的分支代码才会执行。 */
    if (s_rt.active &&
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        (s_rt.state == APP_TASK_STATE_WAIT_APPROACH || s_rt.state == APP_TASK_STATE_AUTH_PASSED)) {
        /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_change_state_locked(APP_TASK_STATE_DOCKING, "docking in progress");

        /* 这里把 true 写入 changed；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        changed = true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 这里开始判断条件 changed；只有条件成立，后面的分支代码才会执行。 */
    if (changed) {

        /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_mark_completed；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_mark_completed(const char *note)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 false 写入 s、rt、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.active = false;

    /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_change_state_locked(APP_TASK_STATE_COMPLETED, note != NULL ? note : "task completed");

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_mark_fault；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_mark_fault(const char *note)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 false 写入 s、rt、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.active = false;

    /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_change_state_locked(APP_TASK_STATE_FAULT, note != NULL ? note : "task fault");

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_cancel；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_cancel(const char *note)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 false 写入 s、rt、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.active = false;

    /* 这里把 0 写入 s、rt、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.matched_tag_id = 0;

    /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_change_state_locked(APP_TASK_STATE_CANCELLED, note != NULL ? note : "task cancelled");

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_reset_idle；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_reset_idle(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 false 写入 s、rt、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.active = false;

    /* 这里把 0 写入 s、rt、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.matched_tag_id = 0;

    /* 调用本项目模块接口 app_task_change_state_locked；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_change_state_locked(APP_TASK_STATE_CONFIGURED, "configured");

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 调用本项目模块接口 app_task_emit_event；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_task_emit_event(APP_TASK_EVENT_STATE_CHANGED);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_get_snapshot；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_task_get_snapshot(app_task_snapshot_t *out)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 out == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (out == NULL) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 进入临界区；这一小段里会屏蔽并发打断，适合保护非常短的共享状态操作。 */
    taskENTER_CRITICAL(&s_mux);

    /* 这里把 s_rt.inited 写入 out、inited；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->inited = s_rt.inited;

    /* 这里把 s_rt.active 写入 out、active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->active = s_rt.active;

    /* 这里把 s_rt.target_dirty 写入 out、目标、dirty；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->target_dirty = s_rt.target_dirty;

    /* 这里把 s_rt.target_id 写入 out、目标、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->target_id = s_rt.target_id;

    /* 这里把 s_rt.matched_tag_id 写入 out、matched、标签、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->matched_tag_id = s_rt.matched_tag_id;

    /* 这里把 s_rt.state 写入 out、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->state = s_rt.state;

    /* 这里把 s_rt.state_since_ms 写入 out、状态、since、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out->state_since_ms = s_rt.state_since_ms;

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(out->source, s_rt.source, sizeof(out->source));

    /* 调用函数 strlcpy；从名字看，它承担的职责和“strlcpy”有关，后续行为取决于这个接口的返回结果或副作用。 */
    strlcpy(out->note, s_rt.note, sizeof(out->note));

    /* 这里把 false 写入 s、rt、目标、dirty；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_rt.target_dirty = false;

    /* 退出临界区；把前面短暂关闭的并发干预重新放开。 */
    taskEXIT_CRITICAL(&s_mux);

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 调用本项目模块接口 app_task_state_to_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
const char *app_task_state_to_text(app_task_state_t state)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (state) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_IDLE:          return "idle";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_CONFIGURED:    return "configured";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_WAIT_APPROACH: return "wait_approach";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_AUTH_PASSED:   return "auth_passed";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_DOCKING:       return "docking";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_COMPLETED:     return "completed";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_FAULT:         return "fault";

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_TASK_STATE_CANCELLED:     return "cancelled";

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:                           return "unknown";
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_task_format_brief；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_task_format_brief(const app_task_snapshot_t *snap, char *buf, size_t buf_len)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 snap == NULL || buf == NULL || buf_len == 0U；只有条件成立，后面的分支代码才会执行。 */
    if (snap == NULL || buf == NULL || buf_len == 0U) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !snap->inited；只有条件成立，后面的分支代码才会执行。 */
    if (!snap->inited) {

        /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
        snprintf(buf, buf_len, "task: not init");

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 按格式把内容写到字符串缓冲区里；它比 sprintf 更安全，因为会限制最大写入长度。 */
    snprintf(buf,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             buf_len,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             "task:%s target:%u src:%s",
             /* 调用本项目模块接口 app_task_state_to_text；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
             app_task_state_to_text(snap->state),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (unsigned)snap->target_id,
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             snap->source[0] != '\0' ? snap->source : "local");
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
