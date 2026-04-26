/*
 * app_video.c - V4L2 摄像头设备底层封装模块（详细注释版）
 *
 * 这个文件负责更底层的视频设备操作：
 * - 打开 /dev/video 设备；
 * - 设置图像格式、分辨率、像素格式；
 * - 把用户分配好的帧缓冲区注册给 V4L2 驱动；
 * - 启动视频流，并在任务内做错误恢复；
 * - 在独立 FreeRTOS 任务里循环 DQBUF -> 处理帧 -> QBUF。
 *
 * app_camera.c 更像业务层显示桥接，本文件更像 Linux/V4L2 风格的摄像头驱动适配层。
 */

#include "freertos/FreeRTOS.h"                     // FreeRTOS 基础定义，任务、队列、事件组等都依赖它。
#include "freertos/task.h"                         // FreeRTOS 任务 API，例如 xTaskCreate、vTaskDelay、任务句柄。
#include <inttypes.h>                              // 跨平台整数格式化宏，方便日志打印固定宽度整数。
#include <string.h>                                // 字符串和内存处理函数，例如 memset、memcpy、strlen、strstr。
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <unistd.h>                                // POSIX 风格接口，例如 close/read/write，在 V4L2 设备操作中常用。
#include "esp_err.h"                               // ESP-IDF 错误码类型 esp_err_t 和 ESP_OK 等定义。
#include "esp_log.h"                               // ESP-IDF 日志系统，提供 ESP_LOGI/ESP_LOGE 等调试输出。
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "bsp/esp-bsp.h"                           // 乐鑫 BSP 通用接口，常用于显示、触摸、音频等板级资源。
#include "app_video.h"                             // 项目自定义模块头文件，声明 app_video 对外提供的接口。
static const char *TAG = "app_video";                            // ESP-IDF 日志标签，串口日志会用它标明当前消息来自哪个模块。
#define MAX_BUFFER_COUNT              6                  // 最大门限，用于限制资源或过滤异常数据。
#define MIN_BUFFER_COUNT              2                  // 最小门限，用于过滤异常数据。
#define VIDEO_TASK_STACK_SIZE         (4 * 1024)         // FreeRTOS 任务栈大小，单位一般是字节。
#define VIDEO_TASK_PRIORITY           6                  // FreeRTOS 任务优先级，数值越大优先级越高。
#define VIDEO_DQBUF_RETRY_DELAY_MS    10
#define VIDEO_QBUF_RETRY_DELAY_MS     10
#define VIDEO_ERROR_RECOVER_THRESHOLD 8
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    uint8_t *camera_buffer[MAX_BUFFER_COUNT];
    size_t camera_buf_size;
    uint32_t camera_buf_hes;
    uint32_t camera_buf_ves;
    struct v4l2_buffer v4l2_buf;
    uint8_t camera_mem_mode;
    app_video_frame_operation_cb_t user_camera_video_frame_operation_cb;
    TaskHandle_t video_stream_task_handle;
    int video_fd_task_arg;
    bool first_frame_logged;
} app_video_t;
static app_video_t s_video;                                      // 模块级静态变量 s_video，只在本文件内部使用，避免被其他文件直接修改。
/*
 * 把 V4L2 fourcc 像素格式转成 4 个字符，方便日志查看当前格式。
 */
static const char *fourcc_to_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xFF);
    out[1] = (char)((fourcc >> 8) & 0xFF);
    out[2] = (char)((fourcc >> 16) & 0xFF);
    out[3] = (char)((fourcc >> 24) & 0xFF);
    out[4] = '\0';
    return out;
}
/*
 * 打开 video 设备并设置分辨率、像素格式等 V4L2 参数。
 */
int app_video_open(char *dev, video_fmt_t init_fmt)
{
    /*
     * V4L2 格式和能力查询结构体。
     * default_format 用于读取驱动当前格式，actual_format 后面会再次读取最终生效格式。
     */
    struct v4l2_format default_format = {0};
    struct v4l2_capability capability = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /*
     * 打开摄像头设备节点。
     * ESP-IDF 的 esp-video 组件提供了类 Linux 的 /dev/video 接口。
     */
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "open video failed");
        return -1;
    }
    // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
        goto err;
    }

    /*
     * 打印驱动能力信息，方便确认当前打开的是哪一个摄像头设备。
     */
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "version: %d.%d.%d",
             (uint16_t)(capability.version >> 16),
             (uint8_t)(capability.version >> 8),
             (uint8_t)capability.version);
    // 信息日志：用于确认程序执行到了哪个阶段。
    ESP_LOGI(TAG, "driver:  %s", capability.driver);
    ESP_LOGI(TAG, "card:    %s", capability.card);
    ESP_LOGI(TAG, "bus:     %s", capability.bus_info);

    /*
     * 读取当前默认格式。
     * 分辨率通常由 BSP/传感器驱动决定，本函数只在需要时切换像素格式。
     */
    default_format.type = type;
    // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
    if (ioctl(fd, VIDIOC_G_FMT, &default_format) != 0) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
        goto err;
    }
    {
        char pix[5];
        // 信息日志：用于确认程序执行到了哪个阶段。
        ESP_LOGI(TAG,
                 "default fmt: %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
                 default_format.fmt.pix.width,
                 default_format.fmt.pix.height,
                 fourcc_to_str(default_format.fmt.pix.pixelformat, pix),
                 default_format.fmt.pix.bytesperline,
                 default_format.fmt.pix.sizeimage);
    }

    /*
     * 如果当前像素格式不是调用者需要的格式，就请求切换。
     * 宽高沿用驱动默认值，避免在这里硬编码传感器分辨率。
     */
    if (default_format.fmt.pix.pixelformat != init_fmt) {
        struct v4l2_format request_format = {
            .type = type,
            .fmt.pix.width = default_format.fmt.pix.width,
            .fmt.pix.height = default_format.fmt.pix.height,
            .fmt.pix.pixelformat = init_fmt,
        };
        // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
        if (ioctl(fd, VIDIOC_S_FMT, &request_format) != 0) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "VIDIOC_S_FMT failed");
            goto err;
        }
    }
#if defined(BSP_CAMERA_VFLIP) && defined(BSP_CAMERA_HFLIP)
    {
        /*
         * 根据 BSP 配置设置摄像头水平/垂直翻转。
         * 这一步用于让屏幕预览方向和实际安装方向一致。
         */
        struct v4l2_ext_control control[1];
        struct v4l2_ext_controls ctrl = {
            .ctrl_class = V4L2_CID_USER_CLASS,
            .count = 1,
            .controls = control,
        };
        control[0].id = V4L2_CID_VFLIP;
        control[0].value = BSP_CAMERA_VFLIP;
        // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
        ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrl);
        control[0].id = V4L2_CID_HFLIP;
        control[0].value = BSP_CAMERA_HFLIP;
        // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
        ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrl);
    }
#endif

    /*
     * 再读一次实际格式。
     * 有些驱动可能无法完全满足请求格式，会返回最接近的格式；
     * 后续缓冲大小必须以实际格式为准。
     */
    struct v4l2_format actual_format = {0};
    actual_format.type = type;
    // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
    if (ioctl(fd, VIDIOC_G_FMT, &actual_format) != 0) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "read actual format failed");
        goto err;
    }
    {
        char pix[5];
        // 信息日志：用于确认程序执行到了哪个阶段。
        ESP_LOGI(TAG,
                 "actual fmt:  %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
                 actual_format.fmt.pix.width,
                 actual_format.fmt.pix.height,
                 fourcc_to_str(actual_format.fmt.pix.pixelformat, pix),
                 actual_format.fmt.pix.bytesperline,
                 actual_format.fmt.pix.sizeimage);
    }

    /*
     * 保存实际图像尺寸，后续 DQBUF 回调会把这些宽高传给 app_camera.c。
     */
    s_video.camera_buf_hes = actual_format.fmt.pix.width;
    s_video.camera_buf_ves = actual_format.fmt.pix.height;

    /*
     * 计算单帧缓冲大小。
     * 优先使用驱动给出的 sizeimage；如果没有，则根据宽高和像素字节数估算。
     */
    uint32_t bytes_per_pixel = 2;
    switch (actual_format.fmt.pix.pixelformat) {
        case APP_VIDEO_FMT_RGB888:
            bytes_per_pixel = 3;
            break;
        case APP_VIDEO_FMT_RGB565:
        default:
            bytes_per_pixel = 2;
            break;
    }
    s_video.camera_buf_size = actual_format.fmt.pix.sizeimage ?
                              actual_format.fmt.pix.sizeimage :
                              (size_t)actual_format.fmt.pix.width * actual_format.fmt.pix.height * bytes_per_pixel;
    return fd;
err:
    /*
     * 任一步失败都关闭 fd，避免设备句柄泄漏。
     */
    close(fd);
    return -1;
}
/*
 * 把上层分配的帧缓冲注册给 V4L2 驱动，使用 USERPTR 模式接收图像。
 */
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb)
{
    /*
     * V4L2 至少需要双缓冲才能一边填充下一帧、一边处理当前帧。
     * 也限制最大数量，避免 s_video.camera_buffer[] 越界。
     */
    if (fb_num > MAX_BUFFER_COUNT) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "buffer num too large");
        return ESP_FAIL;
    }
    if (fb_num < MIN_BUFFER_COUNT) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "at least two buffers are required");
        return ESP_FAIL;
    }

    /*
     * 申请 V4L2 缓冲队列。
     * 如果 fb 非空，使用 USERPTR 模式；否则使用 MMAP 模式。
     */
    struct v4l2_requestbuffers req = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.count = fb_num;
    req.type = type;
    s_video.camera_mem_mode = req.memory = fb ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;
    // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0) {
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        goto err;
    }

    /*
     * 逐个查询/绑定缓冲，并 QBUF 归还给驱动。
     * 驱动只有拿到 QBUF 后，才会开始往这些缓冲里写摄像头帧。
     */
    for (uint32_t i = 0; i < fb_num; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = type;
        buf.memory = req.memory;
        buf.index = i;
        // 调用 V4L2/驱动控制命令，这是视频设备配置和取帧的核心接口。
        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
            goto err;
        }

        /*
         * MMAP 模式：驱动分配缓冲，本端 mmap 映射出来使用。
         */
        if (req.memory == V4L2_MEMORY_MMAP) {
            s_video.camera_buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);
            // 空指针保护：嵌入式代码里不能假设上层传入的指针一定有效。
            if (s_video.camera_buffer[i] == MAP_FAILED || s_video.camera_buffer[i] == NULL) {
                // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
                ESP_LOGE(TAG, "mmap failed");
                goto err;
            }
        } else {
            /*
             * USERPTR 模式：上层已经分配好缓冲，本端把指针交给 V4L2 驱动。
             * app_camera.c 使用这种模式把帧缓冲放到 PSRAM。
             */
            if (!fb || !fb[i]) {
                // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
                ESP_LOGE(TAG, "USERPTR buffer is NULL");
                goto err;
            }
            buf.m.userptr = (unsigned long)fb[i];
            buf.length = s_video.camera_buf_size;
            s_video.camera_buffer[i] = (uint8_t *)fb[i];
        }
        s_video.camera_buf_size = buf.length;
        // V4L2 归还图像缓冲，让驱动继续填充下一帧。
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0) {
            // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
            goto err;
        }
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
err:
    /*
     * 注册缓冲失败后关闭 video fd，让上层重新打开/初始化。
     */
    close(video_fd);
    return ESP_FAIL;
}
/*
 * 返回单帧图像缓冲大小。
 */
uint32_t app_video_get_buf_size(void)
{
    if (s_video.camera_buf_size != 0) {
        return s_video.camera_buf_size;
    }
    return (uint32_t)(s_video.camera_buf_hes * s_video.camera_buf_ves * 2);
}
/*
 * 从 V4L2 队列取出一帧图像，即 DQBUF。
 */
static inline esp_err_t video_receive_video_frame(int video_fd)
{
    memset(&s_video.v4l2_buf, 0, sizeof(s_video.v4l2_buf));
    s_video.v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    s_video.v4l2_buf.memory = s_video.camera_mem_mode;
    // V4L2 取出一帧已完成的图像缓冲。
    if (ioctl(video_fd, VIDIOC_DQBUF, &s_video.v4l2_buf) != 0) {
        // V4L2 取出一帧已完成的图像缓冲。
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "VIDIOC_DQBUF failed errno=%d", errno);
        return ESP_FAIL;
    }
    if (!s_video.first_frame_logged) {
        // 信息日志：用于确认程序执行到了哪个阶段。
        ESP_LOGI(TAG, "first frame: index=%" PRIu32 ", bytesused=%" PRIu32 ", length=%" PRIu32,
                 s_video.v4l2_buf.index,
                 s_video.v4l2_buf.bytesused,
                 s_video.v4l2_buf.length);
        s_video.first_frame_logged = true;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 调用用户注册的帧处理回调，让 app_camera.c 对当前帧做显示/识别处理。
 */
static inline void video_operation_video_frame(void)
{
    const uint8_t buf_index = s_video.v4l2_buf.index;
    if (s_video.user_camera_video_frame_operation_cb) {
        s_video.user_camera_video_frame_operation_cb(
            s_video.camera_buffer[buf_index],
            buf_index,
            s_video.camera_buf_hes,
            s_video.camera_buf_ves,
            s_video.v4l2_buf.bytesused ? s_video.v4l2_buf.bytesused : s_video.camera_buf_size);
    }
}
/*
 * 把处理完的帧重新放回 V4L2 队列，即 QBUF，让摄像头继续写入。
 */
static inline esp_err_t video_free_video_frame(int video_fd)
{
    if (s_video.camera_mem_mode == V4L2_MEMORY_USERPTR) {
        s_video.v4l2_buf.m.userptr = (unsigned long)s_video.camera_buffer[s_video.v4l2_buf.index];
        s_video.v4l2_buf.length = s_video.camera_buf_size;
    }
    // V4L2 归还图像缓冲，让驱动继续填充下一帧。
    if (ioctl(video_fd, VIDIOC_QBUF, &s_video.v4l2_buf) != 0) {
        // V4L2 归还图像缓冲，让驱动继续填充下一帧。
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "VIDIOC_QBUF failed errno=%d", errno);
        return ESP_FAIL;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 通过 VIDIOC_STREAMON 启动摄像头视频流。
 */
static inline esp_err_t video_stream_start(int video_fd)
{
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 通知 V4L2 驱动开始视频流。
    if (ioctl(video_fd, VIDIOC_STREAMON, (void *)&type) != 0) {
        // 通知 V4L2 驱动开始视频流。
        // 错误日志：这类信息通常需要你优先查看，因为它意味着某个关键步骤失败。
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        return ESP_FAIL;
    }
    s_video.first_frame_logged = false;
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 通过 VIDIOC_STREAMOFF 停止摄像头视频流。
 */
static inline esp_err_t video_stream_stop(int video_fd)
{
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 通知 V4L2 驱动停止视频流。
    if (ioctl(video_fd, VIDIOC_STREAMOFF, (void *)&type) != 0) {
        // 通知 V4L2 驱动停止视频流。
        // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
        ESP_LOGW(TAG, "VIDIOC_STREAMOFF failed errno=%d", errno);
        return ESP_FAIL;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 视频流后台任务，循环取帧、处理帧、归还帧。
 */
static void video_stream_task(void *arg)
{
    /*
     * video_fd 通过任务参数传入。
     */
    const int video_fd = *((int *)arg);
    int error_count = 0;

    while (1) {
        /*
         * DQBUF：从驱动取一帧已经写好的图像。
         * 连续失败达到阈值时，尝试重启视频流恢复。
         */
        if (video_receive_video_frame(video_fd) != ESP_OK) {
            error_count++;
            // 任务延时让出 CPU，避免 while 循环空转占满系统。
            vTaskDelay(pdMS_TO_TICKS(VIDEO_DQBUF_RETRY_DELAY_MS));
            if (error_count >= VIDEO_ERROR_RECOVER_THRESHOLD) {
                // 警告日志：系统还能继续运行，但某个功能可能降级或不完整。
                ESP_LOGW(TAG, "too many DQBUF errors, restarting stream");
                video_stream_stop(video_fd);
                // 任务延时让出 CPU，避免 while 循环空转占满系统。
                vTaskDelay(pdMS_TO_TICKS(20));
                video_stream_start(video_fd);
                error_count = 0;
            }
            continue;
        }

        /*
         * 把当前帧交给 app_camera.c 注册的回调。
         */
        video_operation_video_frame();

        /*
         * QBUF：处理完后归还缓冲，驱动才能继续写下一帧。
         */
        if (video_free_video_frame(video_fd) != ESP_OK) {
            error_count++;
            // 任务延时让出 CPU，避免 while 循环空转占满系统。
            vTaskDelay(pdMS_TO_TICKS(VIDEO_QBUF_RETRY_DELAY_MS));
            continue;
        }

        /*
         * DQBUF/QBUF 都成功，清零错误计数。
         */
        error_count = 0;
    }
}
/*
 * 创建视频流任务并启动摄像头采集。
 */
esp_err_t app_video_stream_task_start(int video_fd, int core_id)
{
    if (video_stream_start(video_fd) != ESP_OK) {
        return ESP_FAIL;
    }
    s_video.video_fd_task_arg = video_fd;
    // 创建并固定 FreeRTOS 任务到指定 CPU 核，减少任务迁移带来的抖动。
    BaseType_t ret = xTaskCreatePinnedToCore(video_stream_task,
                                             "video_stream",
                                             VIDEO_TASK_STACK_SIZE,
                                             &s_video.video_fd_task_arg,
                                             VIDEO_TASK_PRIORITY,
                                             &s_video.video_stream_task_handle,
                                             core_id);
    if (ret != pdPASS) {
        video_stream_stop(video_fd);
        return ESP_FAIL;
    }
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
/*
 * 注册每帧图像的处理回调。
 */
esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb)
{
    s_video.user_camera_video_frame_operation_cb = operation_cb;
    // 正常返回 ESP_OK，表示该步骤执行成功。
    return ESP_OK;
}
