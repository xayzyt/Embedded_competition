/*
 * app_video.c - V4L2 摄像头设备底层封装模块
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <unistd.h>
#include "esp_err.h"
#include "esp_log.h"
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "bsp/esp-bsp.h"
#include "app_video.h"

static const char *TAG = "app_video";

/* -------------------------------------------------------------------------- */
/* V4L2 缓冲和任务配置                                          */
/* -------------------------------------------------------------------------- */

#define MAX_BUFFER_COUNT              6
#define MIN_BUFFER_COUNT              2
#define VIDEO_TASK_STACK_SIZE         (4 * 1024)
#define VIDEO_TASK_PRIORITY           6
#define VIDEO_DQBUF_RETRY_DELAY_MS    10
#define VIDEO_QBUF_RETRY_DELAY_MS     10
#define VIDEO_ERROR_RECOVER_THRESHOLD 8

/* -------------------------------------------------------------------------- */
/* 运行状态                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    uint8_t *camera_buffer[MAX_BUFFER_COUNT];                       /* 注册给 V4L2 的帧缓冲地址表。 */
    size_t camera_buf_size;                                         /* 单个摄像头帧缓冲大小。 */
    uint32_t camera_buf_hes;                                        /* 当前帧宽度，保留原 BSP 命名 hes。 */
    uint32_t camera_buf_ves;                                        /* 当前帧高度，保留原 BSP 命名 ves。 */
    struct v4l2_buffer v4l2_buf;                                    /* 当前 DQBUF/QBUF 使用的 V4L2 缓冲描述。 */
    uint8_t camera_mem_mode;                                        /* V4L2 缓冲内存模式，例如 MMAP 或 USERPTR。 */
    app_video_frame_operation_cb_t user_camera_video_frame_operation_cb; /* 上层注册的逐帧处理回调。 */
    TaskHandle_t video_stream_task_handle;                          /* 视频流任务句柄。 */
    int video_fd_task_arg;                                          /* 传给视频流任务的设备 fd。 */
    bool first_frame_logged;                                        /* 是否已经打印过首帧日志。 */
} app_video_t;

static app_video_t s_video;

/* -------------------------------------------------------------------------- */
/* 格式和摄像头控制辅助函数                                           */
/* -------------------------------------------------------------------------- */

static const char *fourcc_to_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xFF);
    out[1] = (char)((fourcc >> 8) & 0xFF);
    out[2] = (char)((fourcc >> 16) & 0xFF);
    out[3] = (char)((fourcc >> 24) & 0xFF);
    out[4] = '\0';
    return out;
}

static int32_t app_video_clamp_ctrl_value(const struct v4l2_query_ext_ctrl *qctrl, int64_t value)
{
    int64_t min_value = qctrl->minimum;
    int64_t max_value = qctrl->maximum;

    if (value < min_value)
    {
        value = min_value;
    }
    else if (value > max_value)
    {
        value = max_value;
    }

    if (qctrl->step > 1 && value > min_value)
    {
        uint64_t offset = (uint64_t)(value - min_value);
        value = min_value + (int64_t)((offset / qctrl->step) * qctrl->step);
    }

    return (int32_t)value;
}

static esp_err_t app_video_set_ext_ctrl_value(int video_fd,
    uint32_t ctrl_class,
    uint32_t ctrl_id,
    int64_t target_value,
    int32_t *applied_value)
{
    struct v4l2_query_ext_ctrl qctrl = {
        .id = ctrl_id,
    };
    if (ioctl(video_fd, VIDIOC_QUERY_EXT_CTRL, &qctrl) != 0)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    struct v4l2_ext_control control[1] = {0};
    struct v4l2_ext_controls controls = {
        .ctrl_class = ctrl_class,
        .count = 1,
        .controls = control,
    };
    int32_t value = app_video_clamp_ctrl_value(&qctrl, target_value);
    control[0].id = ctrl_id;
    control[0].value = value;

    if (ioctl(video_fd, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
        return ESP_FAIL;
    }

    if (applied_value)
    {
        *applied_value = value;
    }
    return ESP_OK;
}

esp_err_t app_video_apply_recognition_profile(int video_fd, uint32_t exposure_us, uint8_t gain_percent)
{
    if (video_fd < 0 || exposure_us == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t applied_exposure = 0;
    uint32_t exposure_units_100us = (exposure_us + 99U) / 100U;
    esp_err_t first_error = app_video_set_ext_ctrl_value(video_fd,
        V4L2_CID_CAMERA_CLASS,
        V4L2_CID_EXPOSURE_ABSOLUTE,
        exposure_units_100us,
        &applied_exposure);

    struct v4l2_query_ext_ctrl gain_qctrl = {
        .id = V4L2_CID_GAIN,
    };
    int32_t applied_gain = 0;
    if (ioctl(video_fd, VIDIOC_QUERY_EXT_CTRL, &gain_qctrl) == 0)
    {
        uint8_t clipped_percent = gain_percent > 100U ? 100U : gain_percent;
        int64_t gain_range = gain_qctrl.maximum - gain_qctrl.minimum;
        int64_t target_gain = gain_qctrl.minimum + (gain_range * clipped_percent) / 100;
        esp_err_t gain_ret = app_video_set_ext_ctrl_value(video_fd,
            V4L2_CID_USER_CLASS,
            V4L2_CID_GAIN,
            target_gain,
            &applied_gain);
        if (first_error == ESP_OK && gain_ret != ESP_OK)
        {
            first_error = gain_ret;
        }
    }
    else if (first_error == ESP_OK)
    {
        first_error = ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGD(TAG,
        "recognition profile exposure=%" PRIi32 "00us gain_index=%" PRIi32,
        applied_exposure,
        applied_gain);
    return first_error;
}

/* -------------------------------------------------------------------------- */
/* 设备初始化                                                                */
/* -------------------------------------------------------------------------- */

int app_video_open(char *dev, video_fmt_t init_fmt)
{
    struct v4l2_format default_format = {0};
    struct v4l2_capability capability = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int fd = open(dev, O_RDWR);
    if (fd < 0)
    {
        ESP_LOGE(TAG, "open video failed");
        return -1;
    }

    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0)
    {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
        goto err;
    }
    ESP_LOGD(TAG, "version: %d.%d.%d",
        (uint16_t)(capability.version >> 16),
        (uint8_t)(capability.version >> 8),
        (uint8_t)capability.version);
    ESP_LOGD(TAG, "driver:  %s", capability.driver);
    ESP_LOGD(TAG, "card:    %s", capability.card);
    ESP_LOGD(TAG, "bus:     %s", capability.bus_info);
    default_format.type = type;

    if (ioctl(fd, VIDIOC_G_FMT, &default_format) != 0)
    {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
        goto err;
    }
    {
        char pix[5];
        ESP_LOGD(TAG,
            "default fmt: %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
            default_format.fmt.pix.width,
            default_format.fmt.pix.height,
            fourcc_to_str(default_format.fmt.pix.pixelformat, pix),
            default_format.fmt.pix.bytesperline,
            default_format.fmt.pix.sizeimage);
    }
    if (default_format.fmt.pix.pixelformat != init_fmt)
    {
        struct v4l2_format request_format = {
            .type = type,
            .fmt.pix.width = default_format.fmt.pix.width,
            .fmt.pix.height = default_format.fmt.pix.height,
            .fmt.pix.pixelformat = init_fmt,
        };

        if (ioctl(fd, VIDIOC_S_FMT, &request_format) != 0)
        {
            ESP_LOGE(TAG, "VIDIOC_S_FMT failed");
            goto err;
        }
    }
#if defined(BSP_CAMERA_VFLIP) && defined(BSP_CAMERA_HFLIP)
    {
        struct v4l2_ext_control control[1];
        struct v4l2_ext_controls ctrl = {
            .ctrl_class = V4L2_CID_USER_CLASS,
            .count = 1,
            .controls = control,
        };
        control[0].id = V4L2_CID_VFLIP;
        control[0].value = BSP_CAMERA_VFLIP;

        ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrl);
        control[0].id = V4L2_CID_HFLIP;
        control[0].value = BSP_CAMERA_HFLIP;

        ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrl);
    }
#endif
    struct v4l2_format actual_format = {0};
    actual_format.type = type;

    if (ioctl(fd, VIDIOC_G_FMT, &actual_format) != 0)
    {
        ESP_LOGE(TAG, "read actual format failed");
        goto err;
    }
    {
        char pix[5];
        ESP_LOGD(TAG,
            "actual fmt:  %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
            actual_format.fmt.pix.width,
            actual_format.fmt.pix.height,
            fourcc_to_str(actual_format.fmt.pix.pixelformat, pix),
            actual_format.fmt.pix.bytesperline,
            actual_format.fmt.pix.sizeimage);
    }
    s_video.camera_buf_hes = actual_format.fmt.pix.width;
    s_video.camera_buf_ves = actual_format.fmt.pix.height;
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
    close(fd);
    return -1;
}
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb)
{
    if (fb_num > MAX_BUFFER_COUNT)
    {
        ESP_LOGE(TAG, "buffer num too large");
        return ESP_FAIL;
    }
    if (fb_num < MIN_BUFFER_COUNT)
    {
        ESP_LOGE(TAG, "at least two buffers are required");
        return ESP_FAIL;
    }
    struct v4l2_requestbuffers req = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.count = fb_num;
    req.type = type;
    s_video.camera_mem_mode = req.memory = fb ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;

    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0)
    {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        goto err;
    }
    for (uint32_t i = 0; i < fb_num; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = type;
        buf.memory = req.memory;
        buf.index = i;

        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0)
        {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
            goto err;
        }
        if (req.memory == V4L2_MEMORY_MMAP)
        {
            s_video.camera_buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);
            if (s_video.camera_buffer[i] == MAP_FAILED || s_video.camera_buffer[i] == NULL)
            {
                ESP_LOGE(TAG, "mmap failed");
                goto err;
            }
        }
        else
        {
            if (!fb || !fb[i])
            {
                ESP_LOGE(TAG, "USERPTR buffer is NULL");
                goto err;
            }
            buf.m.userptr = (unsigned long)fb[i];
            buf.length = s_video.camera_buf_size;
            s_video.camera_buffer[i] = (uint8_t *)fb[i];
        }
        s_video.camera_buf_size = buf.length;

        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0)
        {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
            goto err;
        }
    }
    return ESP_OK;
    err:
    close(video_fd);
    return ESP_FAIL;
}
uint32_t app_video_get_buf_size(void)
{
    if (s_video.camera_buf_size != 0)
    {
        return s_video.camera_buf_size;
    }
    return (uint32_t)(s_video.camera_buf_hes * s_video.camera_buf_ves * 2);
}

/* -------------------------------------------------------------------------- */
/* 视频流循环辅助函数                                                      */
/* -------------------------------------------------------------------------- */

static inline esp_err_t video_receive_video_frame(int video_fd)
{
    memset(&s_video.v4l2_buf, 0, sizeof(s_video.v4l2_buf));
    s_video.v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    s_video.v4l2_buf.memory = s_video.camera_mem_mode;

    if (ioctl(video_fd, VIDIOC_DQBUF, &s_video.v4l2_buf) != 0)
    {

        ESP_LOGW(TAG, "VIDIOC_DQBUF failed errno=%d", errno);
        return ESP_FAIL;
    }
    if (!s_video.first_frame_logged)
    {
        ESP_LOGD(TAG, "first frame: index=%" PRIu32 ", bytesused=%" PRIu32 ", length=%" PRIu32,
            s_video.v4l2_buf.index,
            s_video.v4l2_buf.bytesused,
            s_video.v4l2_buf.length);
        s_video.first_frame_logged = true;
    }
    return ESP_OK;
}
static inline void video_operation_video_frame(void)
{
    const uint8_t buf_index = s_video.v4l2_buf.index;
    if (s_video.user_camera_video_frame_operation_cb)
    {
        s_video.user_camera_video_frame_operation_cb(
            s_video.camera_buffer[buf_index],
            buf_index,
            s_video.camera_buf_hes,
            s_video.camera_buf_ves,
            s_video.v4l2_buf.bytesused ? s_video.v4l2_buf.bytesused : s_video.camera_buf_size);
    }
}
static inline esp_err_t video_free_video_frame(int video_fd)
{
    if (s_video.camera_mem_mode == V4L2_MEMORY_USERPTR)
    {
        s_video.v4l2_buf.m.userptr = (unsigned long)s_video.camera_buffer[s_video.v4l2_buf.index];
        s_video.v4l2_buf.length = s_video.camera_buf_size;
    }

    if (ioctl(video_fd, VIDIOC_QBUF, &s_video.v4l2_buf) != 0)
    {

        ESP_LOGW(TAG, "VIDIOC_QBUF failed errno=%d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}
static inline esp_err_t video_stream_start(int video_fd)
{
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(video_fd, VIDIOC_STREAMON, (void *)&type) != 0)
    {

        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        return ESP_FAIL;
    }
    s_video.first_frame_logged = false;
    return ESP_OK;
}
static inline esp_err_t video_stream_stop(int video_fd)
{
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(video_fd, VIDIOC_STREAMOFF, (void *)&type) != 0)
    {

        ESP_LOGW(TAG, "VIDIOC_STREAMOFF failed errno=%d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void video_stream_restart(int video_fd)
{
    video_stream_stop(video_fd);
    vTaskDelay(pdMS_TO_TICKS(20));
    video_stream_start(video_fd);
}

static void video_stream_task(void *arg)
{
    const int video_fd = *((int *)arg);
    int error_count = 0;

    while (1) {
        if (video_receive_video_frame(video_fd) != ESP_OK)
        {
            error_count++;

            vTaskDelay(pdMS_TO_TICKS(VIDEO_DQBUF_RETRY_DELAY_MS));
            if (error_count >= VIDEO_ERROR_RECOVER_THRESHOLD)
            {
                ESP_LOGW(TAG, "too many DQBUF errors, restarting stream");
                video_stream_restart(video_fd);
                error_count = 0;
            }
            continue;
        }
        video_operation_video_frame();
        if (video_free_video_frame(video_fd) != ESP_OK)
        {
            error_count++;

            vTaskDelay(pdMS_TO_TICKS(VIDEO_QBUF_RETRY_DELAY_MS));
            continue;
        }
        error_count = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t app_video_stream_task_start(int video_fd, int core_id)
{
    if (video_stream_start(video_fd) != ESP_OK)
    {
        return ESP_FAIL;
    }
    s_video.video_fd_task_arg = video_fd;

    BaseType_t ret = xTaskCreatePinnedToCore(video_stream_task,
        "video_stream",
        VIDEO_TASK_STACK_SIZE,
        &s_video.video_fd_task_arg,
        VIDEO_TASK_PRIORITY,
        &s_video.video_stream_task_handle,
        core_id);
    if (ret != pdPASS)
    {
        video_stream_stop(video_fd);
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb)
{
    s_video.user_camera_video_frame_operation_cb = operation_cb;
    return ESP_OK;
}
