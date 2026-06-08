#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <unistd.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "bsp/esp-bsp.h"
#include "app_video.h"

// V4L2 视频封装：负责格式协商、帧缓存注册、取还帧和流任务恢复。

static const char *TAG = "app_video";
#define MAX_BUFFER_COUNT              6
#define MIN_BUFFER_COUNT              2
#define VIDEO_TASK_STACK_SIZE         (4 * 1024)
#define VIDEO_TASK_PRIORITY           6
#define VIDEO_DQBUF_RETRY_DELAY_MS    10
#define VIDEO_QBUF_RETRY_DELAY_MS     10
#define VIDEO_ERROR_RECOVER_THRESHOLD 8
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
static app_video_t s_video;

// 将 fourcc 整数转成可读字符串，便于日志检查实际像素格式。
static const char *fourcc_to_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xFF);
    out[1] = (char)((fourcc >> 8) & 0xFF);
    out[2] = (char)((fourcc >> 16) & 0xFF);
    out[3] = (char)((fourcc >> 24) & 0xFF);
    out[4] = '\0';
    return out;
}
// 打开并配置视频设备；首选分辨率失败时可回退到传感器默认尺寸。
static int app_video_open_configured(char *dev,
    video_fmt_t init_fmt,
    uint32_t preferred_width,
    uint32_t preferred_height,
    bool allow_fallback)
{
    struct v4l2_format default_format = {0};
    struct v4l2_capability capability = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    const bool has_preferred_size = (preferred_width != 0U) && (preferred_height != 0U);
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
    const bool need_set_format =
        (default_format.fmt.pix.pixelformat != init_fmt) ||
        (has_preferred_size &&
            ((default_format.fmt.pix.width != preferred_width) ||
             (default_format.fmt.pix.height != preferred_height)));
    if (need_set_format)
    {
        // 先尝试业务希望的 RGB565/分辨率，失败后按 allow_fallback 决定是否降级。
        struct v4l2_format request_format = {
            .type = type,
            .fmt.pix.width = has_preferred_size ? preferred_width : default_format.fmt.pix.width,
            .fmt.pix.height = has_preferred_size ? preferred_height : default_format.fmt.pix.height,
            .fmt.pix.pixelformat = init_fmt,
        };
        if (ioctl(fd, VIDIOC_S_FMT, &request_format) != 0)
        {
            if (has_preferred_size && allow_fallback)
            {
                ESP_LOGW(TAG,
                    "preferred fmt %" PRIu32 "x%" PRIu32 " rejected, fallback to sensor default",
                    preferred_width,
                    preferred_height);
                close(fd);
                return app_video_open_configured(dev, init_fmt, 0, 0, false);
            }
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
        ESP_LOGI(TAG,
            "actual fmt:  %" PRIu32 "x%" PRIu32 ", pix=%s, bytesperline=%" PRIu32 ", sizeimage=%" PRIu32,
            actual_format.fmt.pix.width,
            actual_format.fmt.pix.height,
            fourcc_to_str(actual_format.fmt.pix.pixelformat, pix),
            actual_format.fmt.pix.bytesperline,
            actual_format.fmt.pix.sizeimage);
    }
    if (has_preferred_size &&
        ((actual_format.fmt.pix.width != preferred_width) ||
         (actual_format.fmt.pix.height != preferred_height)))
    {
        ESP_LOGW(TAG,
            "preferred camera size %" PRIu32 "x%" PRIu32 " not accepted, using %" PRIu32 "x%" PRIu32,
            preferred_width,
            preferred_height,
            actual_format.fmt.pix.width,
            actual_format.fmt.pix.height);
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
int app_video_open_preferred(char *dev,
    video_fmt_t init_fmt,
    uint32_t preferred_width,
    uint32_t preferred_height)
{
    return app_video_open_configured(dev, init_fmt, preferred_width, preferred_height, true);
}
// 配置 MMAP 或 USERPTR 缓冲区，并预先 QBUF 交给驱动填充。
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
// 从驱动取出一帧。
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
        ESP_LOGI(TAG, "first frame: index=%" PRIu32 ", bytesused=%" PRIu32 ", length=%" PRIu32,
            s_video.v4l2_buf.index,
            s_video.v4l2_buf.bytesused,
            s_video.v4l2_buf.length);
        s_video.first_frame_logged = true;
    }
    return ESP_OK;
}
// 把当前帧交给业务回调处理。
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
// 业务处理完后重新入队，让驱动继续复用该帧缓存。
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
// 连续 DQBUF/QBUF 异常时重启流，适配摄像头偶发卡住的情况。
static void video_stream_restart(int video_fd)
{
    video_stream_stop(video_fd);
    vTaskDelay(pdMS_TO_TICKS(20));
    video_stream_start(video_fd);
}
// 视频流任务：DQBUF -> 回调 -> QBUF，失败达到阈值后自动恢复。
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
// 开启 V4L2 流并创建读取任务。
esp_err_t app_video_stream_task_start(int video_fd, int core_id)
{
    if (video_stream_start(video_fd) != ESP_OK)
    {
        return ESP_FAIL;
    }
    s_video.video_fd_task_arg = video_fd;
#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(video_stream_task,
        "video_stream",
        VIDEO_TASK_STACK_SIZE,
        &s_video.video_fd_task_arg,
        VIDEO_TASK_PRIORITY,
        &s_video.video_stream_task_handle,
        core_id,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        ESP_LOGW(TAG, "create video stream task with PSRAM stack failed, try internal stack");
        ret = xTaskCreatePinnedToCore(video_stream_task,
            "video_stream",
            VIDEO_TASK_STACK_SIZE,
            &s_video.video_fd_task_arg,
            VIDEO_TASK_PRIORITY,
            &s_video.video_stream_task_handle,
            core_id);
    }
#else
    BaseType_t ret = xTaskCreatePinnedToCore(video_stream_task,
        "video_stream",
        VIDEO_TASK_STACK_SIZE,
        &s_video.video_fd_task_arg,
        VIDEO_TASK_PRIORITY,
        &s_video.video_stream_task_handle,
        core_id);
#endif
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
