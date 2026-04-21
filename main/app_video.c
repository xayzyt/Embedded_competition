/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */







/* 引入 freertos/FreeRTOS.h；FreeRTOS 核心头文件；任务、队列、事件组等内核对象的基础定义都依赖它。 */
#include "freertos/FreeRTOS.h"
/* 引入 freertos/task.h；FreeRTOS 任务头文件；xTaskCreate、vTaskDelay、任务通知等接口主要在这里声明。 */
#include "freertos/task.h"
/* 引入 freertos/event_groups.h；FreeRTOS 事件组头文件；多个标志位同步时比队列更合适。 */
#include "freertos/event_groups.h"

/* 引入 inttypes.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <inttypes.h>
/* 引入 string.h；标准字符串/内存处理头文件；memcpy、memset、strcmp 等基础接口都来自这里。 */
#include <string.h>
/* 引入 fcntl.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <fcntl.h>
/* 引入 sys/ioctl.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <sys/ioctl.h>
/* 引入 sys/mman.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <sys/mman.h>
/* 引入 sys/param.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <sys/param.h>
/* 引入 sys/errno.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <sys/errno.h>
/* 引入 unistd.h；POSIX 基础接口头文件；常见于 sleep/usleep 一类延时或系统调用封装。 */
#include <unistd.h>

/* 引入 esp_err.h；ESP-IDF 错误码头文件；esp_err_t、ESP_OK、ESP_ERROR_CHECK 等错误处理机制依赖它。 */
#include "esp_err.h"
/* 引入 esp_log.h；ESP-IDF 日志头文件；ESP_LOGI/ESP_LOGW/ESP_LOGE 这些日志宏都从这里来。 */
#include "esp_log.h"
/* 引入 linux/videodev2.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "linux/videodev2.h"
/* 引入 esp_video_init.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include "esp_video_init.h"
/* 引入 bsp/esp-bsp.h；乐鑫 BSP 总头文件；板级资源封装通常会从这里统一引入。 */
#include "bsp/esp-bsp.h"

/* 引入本项目的 app_video 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_video.h"














/* 这里把 "app_video" 写入 static、const、char、标签；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static const char *TAG = "app_video";




















































/* 定义宏 MAX_BUFFER_COUNT；这里把“最大、缓冲区、计数”集中写成常量 6，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define MAX_BUFFER_COUNT              6
/* 定义宏 MIN_BUFFER_COUNT；这里把“最小、缓冲区、计数”集中写成常量 2，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define MIN_BUFFER_COUNT              2
/* 定义宏 VIDEO_TASK_STACK_SIZE；这里把“视频、任务、STACK、大小”集中写成常量 (4 * 1024)，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VIDEO_TASK_STACK_SIZE         (4 * 1024)
/* 定义宏 VIDEO_TASK_PRIORITY；这里把“视频、任务、PRIORITY”集中写成常量 6，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VIDEO_TASK_PRIORITY           6
/* 定义宏 VIDEO_DQBUF_RETRY_DELAY_MS；这里把“视频、DQBUF、RETRY、DELAY、毫秒”集中写成常量 10，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VIDEO_DQBUF_RETRY_DELAY_MS    10
/* 定义宏 VIDEO_QBUF_RETRY_DELAY_MS；这里把“视频、QBUF、RETRY、DELAY、毫秒”集中写成常量 10，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VIDEO_QBUF_RETRY_DELAY_MS     10
/* 定义宏 VIDEO_ERROR_RECOVER_THRESHOLD；这里把“视频、ERROR、RECOVER、THRESHOLD”集中写成常量 8，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define VIDEO_ERROR_RECOVER_THRESHOLD 8



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef enum {

    /* 这里把 BIT(0), 写入 视频、任务、DELETE；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    VIDEO_TASK_DELETE      = BIT(0),

    /* 这里把 BIT(1), 写入 视频、任务、DELETE、DONE；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    VIDEO_TASK_DELETE_DONE = BIT(1),

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} video_event_id_t;



/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
typedef struct {

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    uint8_t *camera_buffer[MAX_BUFFER_COUNT];

    /* 这里先定义变量 camera_buf_size，类型是 size_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    size_t camera_buf_size;

    /* 这里先定义变量 camera_buf_hes，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t camera_buf_hes;

    /* 这里先定义变量 camera_buf_ves，类型是 uint32_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint32_t camera_buf_ves;

    /* 这里先定义变量 v4l2_buf，类型是 struct v4l2_buffer；后面真正给它赋值或填内容的代码会继续跟上。 */
    struct v4l2_buffer v4l2_buf;

    /* 这里先定义变量 camera_mem_mode，类型是 uint8_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    uint8_t camera_mem_mode;

    /* 这里先定义变量 user_camera_video_frame_operation_cb，类型是 app_video_frame_operation_cb_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    app_video_frame_operation_cb_t user_camera_video_frame_operation_cb;

    /* 这里先定义变量 video_stream_task_handle，类型是 TaskHandle_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    TaskHandle_t video_stream_task_handle;

    /* 这里先定义变量 video_event_group，类型是 EventGroupHandle_t；后面真正给它赋值或填内容的代码会继续跟上。 */
    EventGroupHandle_t video_event_group;

    /* 这里先定义变量 video_fd_task_arg，类型是 int；后面真正给它赋值或填内容的代码会继续跟上。 */
    int video_fd_task_arg;

    /* 这里先定义变量 first_frame_logged，类型是 bool；后面真正给它赋值或填内容的代码会继续跟上。 */
    bool first_frame_logged;

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
} app_video_t;


/* 这里先定义变量 s_video，类型是 static app_video_t；后面真正给它赋值或填内容的代码会继续跟上。 */
static app_video_t s_video;






/* 这里开始定义函数 app_video_main；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_main(i2c_master_bus_handle_t i2c_bus_handle)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)i2c_bus_handle;

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}


/* 调用函数 fourcc_to_str；从名字看，它承担的职责和“fourcc、to、str”有关，后续行为取决于这个接口的返回结果或副作用。 */
static const char *fourcc_to_str(uint32_t fourcc, char out[5])
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 (char)(fourcc & 0xFF) 写入 out、0；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out[0] = (char)(fourcc & 0xFF);

    /* 这里把 (char)((fourcc >> 8) & 0xFF) 写入 out、1；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out[1] = (char)((fourcc >> 8) & 0xFF);

    /* 这里把 (char)((fourcc >> 16) & 0xFF) 写入 out、2；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out[2] = (char)((fourcc >> 16) & 0xFF);

    /* 这里把 (char)((fourcc >> 24) & 0xFF) 写入 out、3；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out[3] = (char)((fourcc >> 24) & 0xFF);

    /* 这里把 '\0' 写入 out、4；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    out[4] = '\0';

    /* 这里把 out 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return out;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_open；返回类型是 int，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
int app_video_open(char *dev, video_fmt_t init_fmt)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 default_format，类型是 struct v4l2_format，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    struct v4l2_format default_format = {0};

    /* 这里定义变量 capability，类型是 struct v4l2_capability，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    struct v4l2_capability capability = {0};

    /* 这里定义变量 type，类型是 const int，并且在声明时就把初值设成 V4L2_BUF_TYPE_VIDEO_CAPTURE；这样后面第一次使用它时就是一个确定状态。 */
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;


    /* 这里定义变量 fd，类型是 int，并且在声明时就把初值设成 open(dev, O_RDONLY)；这样后面第一次使用它时就是一个确定状态。 */
    int fd = open(dev, O_RDONLY);

    /* 这里开始判断条件 fd < 0；只有条件成立，后面的分支代码才会执行。 */
    if (fd < 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "open video failed");

        /* 这里把 -1 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return -1;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");

        /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
        goto err;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "version: %d.%d.%d",
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (uint16_t)(capability.version >> 16),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (uint8_t)(capability.version >> 8),
             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
             (uint8_t)capability.version);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "driver:  %s", capability.driver);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "card:    %s", capability.card);

    /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
    ESP_LOGI(TAG, "bus:     %s", capability.bus_info);


    /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
    default_format.type = type;

    /* 这里开始判断条件 ioctl(fd, VIDIOC_G_FMT, &default_format) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(fd, VIDIOC_G_FMT, &default_format) != 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed");

        /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
        goto err;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
    {

        /* 这里先定义变量 pix，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
        char pix[5];

        /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
        ESP_LOGI(TAG,
                 /* 这里把 %s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32, 写入 默认、fmt、PRIu32、x、PRIu32、pix；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                 "default fmt: %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
                 /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
                 default_format.fmt.pix.width,
                 /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
                 default_format.fmt.pix.height,
                 /* 调用函数 fourcc_to_str；从名字看，它承担的职责和“fourcc、to、str”有关，后续行为取决于这个接口的返回结果或副作用。 */
                 fourcc_to_str(default_format.fmt.pix.pixelformat, pix),
                 /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
                 default_format.fmt.pix.bytesperline,
                 /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
                 default_format.fmt.pix.sizeimage);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 default_format.fmt.pix.pixelformat != init_fmt；只有条件成立，后面的分支代码才会执行。 */
    if (default_format.fmt.pix.pixelformat != init_fmt) {
        /* 这里定义变量 request_format，类型是 struct v4l2_format，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
        struct v4l2_format request_format = {
            /* 这里把 type, 写入 类型；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .type = type,
            /* 这里把 default_format.fmt.pix.width, 写入 fmt、pix、width；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .fmt.pix.width = default_format.fmt.pix.width,
            /* 这里把 default_format.fmt.pix.height, 写入 fmt、pix、height；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .fmt.pix.height = default_format.fmt.pix.height,
            /* 这里把 init_fmt, 写入 fmt、pix、pixelformat；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .fmt.pix.pixelformat = init_fmt,
        /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
        };

        /* 这里开始判断条件 ioctl(fd, VIDIOC_S_FMT, &request_format) != 0；只有条件成立，后面的分支代码才会执行。 */
        if (ioctl(fd, VIDIOC_S_FMT, &request_format) != 0) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "VIDIOC_S_FMT failed");

            /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
            goto err;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if defined(BSP_CAMERA_VFLIP) && defined(BSP_CAMERA_HFLIP)
    /* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
    {

        /* 这里先定义变量 control，类型是 struct v4l2_ext_control；后面真正给它赋值或填内容的代码会继续跟上。 */
        struct v4l2_ext_control control[1];
        /* 这里定义变量 ctrl，类型是 struct v4l2_ext_controls，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
        struct v4l2_ext_controls ctrl = {
            /* 这里把 V4L2_CID_USER_CLASS, 写入 控制、class；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .ctrl_class = V4L2_CID_USER_CLASS,
            /* 这里把 1, 写入 计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .count = 1,
            /* 这里把 control, 写入 controls；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            .controls = control,
        /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
        };


        /* 这里把 V4L2_CID_VFLIP 写入 control、0、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        control[0].id = V4L2_CID_VFLIP;

        /* 这里把 BSP_CAMERA_VFLIP 写入 control、0、value；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        control[0].value = BSP_CAMERA_VFLIP;

        /* 调用函数 ioctl；从名字看，它承担的职责和“ioctl”有关，后续行为取决于这个接口的返回结果或副作用。 */
        ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrl);


        /* 这里把 V4L2_CID_HFLIP 写入 control、0、ID；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        control[0].id = V4L2_CID_HFLIP;

        /* 这里把 BSP_CAMERA_HFLIP 写入 control、0、value；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        control[0].value = BSP_CAMERA_HFLIP;

        /* 调用函数 ioctl；从名字看，它承担的职责和“ioctl”有关，后续行为取决于这个接口的返回结果或副作用。 */
        ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrl);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif


    /* 这里定义变量 actual_format，类型是 struct v4l2_format，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    struct v4l2_format actual_format = {0};

    /* 这里把 type 写入 actual、format、类型；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    actual_format.type = type;

    /* 这里开始判断条件 ioctl(fd, VIDIOC_G_FMT, &actual_format) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(fd, VIDIOC_G_FMT, &actual_format) != 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "read actual format failed");

        /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
        goto err;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
    {

        /* 这里先定义变量 pix，类型是 char；后面真正给它赋值或填内容的代码会继续跟上。 */
        char pix[5];

        /* 打印一条 INFO 级日志；这里通常用于告诉你流程已经走到哪个阶段。 */
        ESP_LOGI(TAG,
                 /* 这里把 %s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32, 写入 actual、fmt、PRIu32、x、PRIu32、pix；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                 "actual fmt:  %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 actual_format.fmt.pix.width,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 actual_format.fmt.pix.height,
                 /* 调用函数 fourcc_to_str；从名字看，它承担的职责和“fourcc、to、str”有关，后续行为取决于这个接口的返回结果或副作用。 */
                 fourcc_to_str(actual_format.fmt.pix.pixelformat, pix),
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 actual_format.fmt.pix.bytesperline,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 actual_format.fmt.pix.sizeimage);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 actual_format.fmt.pix.width 写入 s、视频、相机、缓冲区、hes；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.camera_buf_hes = actual_format.fmt.pix.width;

    /* 这里把 actual_format.fmt.pix.height 写入 s、视频、相机、缓冲区、ves；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.camera_buf_ves = actual_format.fmt.pix.height;


    /* 这里定义变量 bytes_per_pixel，类型是 uint32_t，并且在声明时就把初值设成 2；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t bytes_per_pixel = 2;

    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (actual_format.fmt.pix.pixelformat) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_VIDEO_FMT_RGB888:

            /* 这里把 3 写入 bytes、per、pixel；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            bytes_per_pixel = 3;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_VIDEO_FMT_RGB565:

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 这里把 2 写入 bytes、per、pixel；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            bytes_per_pixel = 2;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 actual_format.fmt.pix.sizeimage ? 写入 s、视频、相机、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.camera_buf_size = actual_format.fmt.pix.sizeimage ?
                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                              actual_format.fmt.pix.sizeimage :

                              /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                              (size_t)actual_format.fmt.pix.width * actual_format.fmt.pix.height * bytes_per_pixel;

    /* 这里把 fd 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return fd;

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
err:

    /* 调用函数 close；从名字看，它承担的职责和“关闭”有关，后续行为取决于这个接口的返回结果或副作用。 */
    close(fd);

    /* 这里把 -1 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return -1;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_set_bufs；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 fb_num > MAX_BUFFER_COUNT；只有条件成立，后面的分支代码才会执行。 */
    if (fb_num > MAX_BUFFER_COUNT) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "buffer num too large");

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 fb_num < MIN_BUFFER_COUNT；只有条件成立，后面的分支代码才会执行。 */
    if (fb_num < MIN_BUFFER_COUNT) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "at least two buffers are required");

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 req，类型是 struct v4l2_requestbuffers，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
    struct v4l2_requestbuffers req = {0};

    /* 这里定义变量 type，类型是 const int，并且在声明时就把初值设成 V4L2_BUF_TYPE_VIDEO_CAPTURE；这样后面第一次使用它时就是一个确定状态。 */
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;


    /* 这里把 fb_num 写入 req、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    req.count = fb_num;

    /* 这里把 type 写入 req、类型；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    req.type = type;

    /* 这里把 req.memory = fb ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP 写入 s、视频、相机、mem、模式；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.camera_mem_mode = req.memory = fb ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;


    /* 这里开始判断条件 ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");

        /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
        goto err;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (uint32_t i = 0; i < fb_num; i++) {

        /* 这里定义变量 buf，类型是 struct v4l2_buffer，并且在声明时就把初值设成 {0}；这样后面第一次使用它时就是一个确定状态。 */
        struct v4l2_buffer buf = {0};

        /* 这里把 type 写入 缓冲区、类型；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        buf.type = type;

        /* 这里把 req.memory 写入 缓冲区、memory；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        buf.memory = req.memory;

        /* 这里把 i 写入 缓冲区、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        buf.index = i;


        /* 这里开始判断条件 ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0；只有条件成立，后面的分支代码才会执行。 */
        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");

            /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
            goto err;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 req.memory == V4L2_MEMORY_MMAP；只有条件成立，后面的分支代码才会执行。 */
        if (req.memory == V4L2_MEMORY_MMAP) {

            /* 这里把 mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset) 写入 s、视频、相机、缓冲区、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_video.camera_buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);

            /* 这里开始判断条件 s_video.camera_buffer[i] == MAP_FAILED || s_video.camera_buffer[i] == NULL；只有条件成立，后面的分支代码才会执行。 */
            if (s_video.camera_buffer[i] == MAP_FAILED || s_video.camera_buffer[i] == NULL) {

                /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
                ESP_LOGE(TAG, "mmap failed");

                /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
                goto err;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 这里开始判断条件 !fb || !fb[i]；只有条件成立，后面的分支代码才会执行。 */
            if (!fb || !fb[i]) {

                /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
                ESP_LOGE(TAG, "USERPTR buffer is NULL");

                /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
                goto err;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里把 (unsigned long)fb[i] 写入 缓冲区、m、userptr；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            buf.m.userptr = (unsigned long)fb[i];

            /* 这里把 s_video.camera_buf_size 写入 缓冲区、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            buf.length = s_video.camera_buf_size;

            /* 这里把 (uint8_t *)fb[i] 写入 s、视频、相机、缓冲区、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_video.camera_buffer[i] = (uint8_t *)fb[i];
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里把 buf.length 写入 s、视频、相机、缓冲区、大小；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video.camera_buf_size = buf.length;


        /* 这里开始判断条件 ioctl(video_fd, VIDIOC_QBUF, &buf) != 0；只有条件成立，后面的分支代码才会执行。 */
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0) {

            /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");

            /* 这里使用 goto 直接跳到指定标签；在嵌入式里常见于统一清理资源或集中错误退出。 */
            goto err;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;

/* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
err:

    /* 调用函数 close；从名字看，它承担的职责和“关闭”有关，后续行为取决于这个接口的返回结果或副作用。 */
    close(video_fd);

    /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_FAIL;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_get_bufs；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_get_bufs(int fb_num, void **fb)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 fb_num > MAX_BUFFER_COUNT || fb_num < MIN_BUFFER_COUNT；只有条件成立，后面的分支代码才会执行。 */
    if (fb_num > MAX_BUFFER_COUNT || fb_num < MIN_BUFFER_COUNT) {

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < fb_num; i++) {

        /* 这里开始判断条件 s_video.camera_buffer[i] == NULL；只有条件成立，后面的分支代码才会执行。 */
        if (s_video.camera_buffer[i] == NULL) {

            /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ESP_FAIL;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }

        /* 这里把 s_video.camera_buffer[i] 写入 fb、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        fb[i] = s_video.camera_buffer[i];
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_get_buf_size；返回类型是 uint32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
uint32_t app_video_get_buf_size(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_video.camera_buf_size != 0；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.camera_buf_size != 0) {

        /* 这里把 s_video.camera_buf_size 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return s_video.camera_buf_size;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 (uint32_t)(s_video.camera_buf_hes * s_video.camera_buf_ves * 2) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return (uint32_t)(s_video.camera_buf_hes * s_video.camera_buf_ves * 2);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 video_receive_video_frame；返回类型是 static inline esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline esp_err_t video_receive_video_frame(int video_fd)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 把一段内存按字节填成指定值；最常见的用途是清零结构体或缓冲区。 */
    memset(&s_video.v4l2_buf, 0, sizeof(s_video.v4l2_buf));

    /* 这里把 V4L2_BUF_TYPE_VIDEO_CAPTURE 写入 s、视频、v4l2、缓冲区、类型；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* 这里把 s_video.camera_mem_mode 写入 s、视频、v4l2、缓冲区、memory；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.v4l2_buf.memory = s_video.camera_mem_mode;


    /* 这里开始判断条件 ioctl(video_fd, VIDIOC_DQBUF, &s_video.v4l2_buf) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(video_fd, VIDIOC_DQBUF, &s_video.v4l2_buf) != 0) {

        /* 这里把 %d", errno) 写入 ESP、LOGW、标签、VIDIOC、DQBUF、failed、errno；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ESP_LOGW(TAG, "VIDIOC_DQBUF failed errno=%d", errno);

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !s_video.first_frame_logged；只有条件成立，后面的分支代码才会执行。 */
    if (!s_video.first_frame_logged) {

        /* 这里把 %" PRIu32 ", bytesused=%" PRIu32 ", length=%" PRIu32, 写入 ESP、LOGI、标签、first、帧、索引；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ESP_LOGI(TAG, "first frame: index=%" PRIu32 ", bytesused=%" PRIu32 ", length=%" PRIu32,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 s_video.v4l2_buf.index,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 s_video.v4l2_buf.bytesused,
                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                 s_video.v4l2_buf.length);

        /* 这里把 true 写入 s、视频、first、帧、logged；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video.first_frame_logged = true;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 video_operation_video_frame；返回类型是 static inline void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline void video_operation_video_frame(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 buf_index，类型是 const uint8_t，并且在声明时就把初值设成 s_video.v4l2_buf.index；这样后面第一次使用它时就是一个确定状态。 */
    const uint8_t buf_index = s_video.v4l2_buf.index;

    /* 这里开始判断条件 s_video.user_camera_video_frame_operation_cb；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.user_camera_video_frame_operation_cb) {
        /* 调用函数 user_camera_video_frame_operation_cb；从名字看，它承担的职责和“user、相机、视频、帧、operation、cb”有关，后续行为取决于这个接口的返回结果或副作用。 */
        s_video.user_camera_video_frame_operation_cb(
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            s_video.camera_buffer[buf_index],
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            buf_index,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            s_video.camera_buf_hes,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            s_video.camera_buf_ves,
            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            s_video.v4l2_buf.bytesused ? s_video.v4l2_buf.bytesused : s_video.camera_buf_size);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 video_free_video_frame；返回类型是 static inline esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline esp_err_t video_free_video_frame(int video_fd)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_video.camera_mem_mode == V4L2_MEMORY_USERPTR；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.camera_mem_mode == V4L2_MEMORY_USERPTR) {

        /* 这里把 (unsigned long)s_video.camera_buffer[s_video.v4l2_buf.index] 写入 s、视频、v4l2、缓冲区、m、userptr；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video.v4l2_buf.m.userptr = (unsigned long)s_video.camera_buffer[s_video.v4l2_buf.index];

        /* 这里把 s_video.camera_buf_size 写入 s、视频、v4l2、缓冲区、长度；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video.v4l2_buf.length = s_video.camera_buf_size;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 ioctl(video_fd, VIDIOC_QBUF, &s_video.v4l2_buf) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(video_fd, VIDIOC_QBUF, &s_video.v4l2_buf) != 0) {

        /* 这里把 %d", errno) 写入 ESP、LOGW、标签、VIDIOC、QBUF、failed、errno；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ESP_LOGW(TAG, "VIDIOC_QBUF failed errno=%d", errno);

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 video_stream_start；返回类型是 static inline esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline esp_err_t video_stream_start(int video_fd)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 type，类型是 const int，并且在声明时就把初值设成 V4L2_BUF_TYPE_VIDEO_CAPTURE；这样后面第一次使用它时就是一个确定状态。 */
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* 这里开始判断条件 ioctl(video_fd, VIDIOC_STREAMON, (void *)&type) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(video_fd, VIDIOC_STREAMON, (void *)&type) != 0) {

        /* 打印一条 ERROR 级日志；说明这里已经出现明显错误，后面通常会回退、返回或停机。 */
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 false 写入 s、视频、first、帧、logged；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.first_frame_logged = false;

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 video_stream_stop；返回类型是 static inline esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static inline esp_err_t video_stream_stop(int video_fd)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 type，类型是 const int，并且在声明时就把初值设成 V4L2_BUF_TYPE_VIDEO_CAPTURE；这样后面第一次使用它时就是一个确定状态。 */
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* 这里开始判断条件 ioctl(video_fd, VIDIOC_STREAMOFF, (void *)&type) != 0；只有条件成立，后面的分支代码才会执行。 */
    if (ioctl(video_fd, VIDIOC_STREAMOFF, (void *)&type) != 0) {

        /* 这里把 %d", errno) 写入 ESP、LOGW、标签、VIDIOC、STREAMOFF、failed、errno；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        ESP_LOGW(TAG, "VIDIOC_STREAMOFF failed errno=%d", errno);

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_video.video_event_group；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.video_event_group) {

        /* 调用函数 xEventGroupSetBits；从名字看，它承担的职责和“xEventGroupSetBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
        xEventGroupSetBits(s_video.video_event_group, VIDEO_TASK_DELETE_DONE);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 video_stream_task；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void video_stream_task(void *arg)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 video_fd，类型是 const int，并且在声明时就把初值设成 *((int *)arg)；这样后面第一次使用它时就是一个确定状态。 */
    const int video_fd = *((int *)arg);

    /* 这里定义变量 error_count，类型是 int，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int error_count = 0;


    /* 这里开始一个 while 循环；只要条件一直成立，就会反复执行下面这段逻辑。 */
    while (1) {

        /* 这里开始判断条件 xEventGroupGetBits(s_video.video_event_group) & VIDEO_TASK_DELETE；只有条件成立，后面的分支代码才会执行。 */
        if (xEventGroupGetBits(s_video.video_event_group) & VIDEO_TASK_DELETE) {

            /* 调用函数 xEventGroupClearBits；从名字看，它承担的职责和“xEventGroupClearBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
            xEventGroupClearBits(s_video.video_event_group, VIDEO_TASK_DELETE);

            /* 调用函数 video_stream_stop；从名字看，它承担的职责和“视频、stream、停止”有关，后续行为取决于这个接口的返回结果或副作用。 */
            video_stream_stop(video_fd);

            /* 调用 FreeRTOS 任务接口 vTaskDelete；这里通常在创建任务、延时或查询任务运行状态。 */
            vTaskDelete(NULL);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 video_receive_video_frame(video_fd) != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
        if (video_receive_video_frame(video_fd) != ESP_OK) {

            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            error_count++;

            /* 让当前 FreeRTOS 任务主动让出 CPU 一段时间；这样不会像 while 死等那样把系统卡住。 */
            vTaskDelay(pdMS_TO_TICKS(VIDEO_DQBUF_RETRY_DELAY_MS));

            /* 这里开始判断条件 error_count >= VIDEO_ERROR_RECOVER_THRESHOLD；只有条件成立，后面的分支代码才会执行。 */
            if (error_count >= VIDEO_ERROR_RECOVER_THRESHOLD) {

                /* 打印一条 WARN 级日志；说明程序还能继续跑，但这里有需要注意的风险。 */
                ESP_LOGW(TAG, "too many DQBUF errors, restarting stream");

                /* 调用函数 video_stream_stop；从名字看，它承担的职责和“视频、stream、停止”有关，后续行为取决于这个接口的返回结果或副作用。 */
                video_stream_stop(video_fd);

                /* 让当前 FreeRTOS 任务主动让出 CPU 一段时间；这样不会像 while 死等那样把系统卡住。 */
                vTaskDelay(pdMS_TO_TICKS(20));

                /* 调用函数 video_stream_start；从名字看，它承担的职责和“视频、stream、启动”有关，后续行为取决于这个接口的返回结果或副作用。 */
                video_stream_start(video_fd);

                /* 这里把 0 写入 error、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
                error_count = 0;
            /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
            }

            /* 这里直接结束当前这一轮循环，马上进入下一轮；常用于过滤掉当前不想处理的数据。 */
            continue;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 调用函数 video_operation_video_frame；从名字看，它承担的职责和“视频、operation、视频、帧”有关，后续行为取决于这个接口的返回结果或副作用。 */
        video_operation_video_frame();


        /* 这里开始判断条件 video_free_video_frame(video_fd) != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
        if (video_free_video_frame(video_fd) != ESP_OK) {

            /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
            error_count++;

            /* 让当前 FreeRTOS 任务主动让出 CPU 一段时间；这样不会像 while 死等那样把系统卡住。 */
            vTaskDelay(pdMS_TO_TICKS(VIDEO_QBUF_RETRY_DELAY_MS));

            /* 这里直接结束当前这一轮循环，马上进入下一轮；常用于过滤掉当前不想处理的数据。 */
            continue;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里把 0 写入 error、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        error_count = 0;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_stream_task_start；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_stream_task_start(int video_fd, int core_id)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_video.video_event_group == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.video_event_group == NULL) {

        /* 这里把 xEventGroupCreate() 写入 s、视频、视频、事件、组；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_video.video_event_group = xEventGroupCreate();

        /* 这里开始判断条件 s_video.video_event_group == NULL；只有条件成立，后面的分支代码才会执行。 */
        if (s_video.video_event_group == NULL) {

            /* 这里把 ESP_ERR_NO_MEM 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return ESP_ERR_NO_MEM;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用函数 xEventGroupClearBits；从名字看，它承担的职责和“xEventGroupClearBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
    xEventGroupClearBits(s_video.video_event_group, VIDEO_TASK_DELETE_DONE);


    /* 这里开始判断条件 video_stream_start(video_fd) != ESP_OK；只有条件成立，后面的分支代码才会执行。 */
    if (video_stream_start(video_fd) != ESP_OK) {

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 video_fd 写入 s、视频、视频、fd、任务、arg；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.video_fd_task_arg = video_fd;


    /* 这里定义变量 ret，类型是 BaseType_t，并且在声明时就把初值设成 xTaskCreatePinnedToCore(video_stream_task,；这样后面第一次使用它时就是一个确定状态。 */
    BaseType_t ret = xTaskCreatePinnedToCore(video_stream_task,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             "video_stream",
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             VIDEO_TASK_STACK_SIZE,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             &s_video.video_fd_task_arg,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             VIDEO_TASK_PRIORITY,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             &s_video.video_stream_task_handle,
                                             /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                             core_id);

    /* 这里开始判断条件 ret != pdPASS；只有条件成立，后面的分支代码才会执行。 */
    if (ret != pdPASS) {

        /* 调用函数 video_stream_stop；从名字看，它承担的职责和“视频、stream、停止”有关，后续行为取决于这个接口的返回结果或副作用。 */
        video_stream_stop(video_fd);

        /* 这里把 ESP_FAIL 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_FAIL;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_stream_task_stop；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_stream_task_stop(int video_fd)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)video_fd;

    /* 这里开始判断条件 s_video.video_event_group；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.video_event_group) {

        /* 调用函数 xEventGroupSetBits；从名字看，它承担的职责和“xEventGroupSetBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
        xEventGroupSetBits(s_video.video_event_group, VIDEO_TASK_DELETE);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_register_frame_operation_cb；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里把 operation_cb 写入 s、视频、user、相机、视频、帧、operation、cb；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_video.user_camera_video_frame_operation_cb = operation_cb;

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_video_stream_wait_stop；返回类型是 esp_err_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
esp_err_t app_video_stream_wait_stop(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 s_video.video_event_group == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_video.video_event_group == NULL) {

        /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return ESP_OK;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用函数 xEventGroupWaitBits；从名字看，它承担的职责和“xEventGroupWaitBits”有关，后续行为取决于这个接口的返回结果或副作用。 */
    xEventGroupWaitBits(s_video.video_event_group,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        VIDEO_TASK_DELETE_DONE,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        pdTRUE,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        pdTRUE,
                        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                        portMAX_DELAY);

    /* 这里把 ESP_OK 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return ESP_OK;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
