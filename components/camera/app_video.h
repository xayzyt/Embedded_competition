#ifndef APP_VIDEO_H
#define APP_VIDEO_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "linux/videodev2.h"
#include "esp_video_device.h"
// V4L2 视频层接口：屏蔽设备打开、格式协商、缓冲注册和流任务细节。

#ifdef __cplusplus
extern "C" {
#endif
// V4L2 像素格式封装，业务层统一使用 video_fmt_t。
typedef enum {
    APP_VIDEO_FMT_RAW8   = V4L2_PIX_FMT_SBGGR8,
    APP_VIDEO_FMT_RAW10  = V4L2_PIX_FMT_SBGGR10,
    APP_VIDEO_FMT_GREY   = V4L2_PIX_FMT_GREY,
    APP_VIDEO_FMT_RGB565 = V4L2_PIX_FMT_RGB565,
    APP_VIDEO_FMT_RGB888 = V4L2_PIX_FMT_RGB24,
    APP_VIDEO_FMT_YUV422 = V4L2_PIX_FMT_YUV422P,
    APP_VIDEO_FMT_YUV420 = V4L2_PIX_FMT_YUV420,
} video_fmt_t;
// 每帧视频数据到达后的业务回调。
typedef void (*app_video_frame_operation_cb_t)(uint8_t *camera_buf,
                                               uint8_t camera_buf_index,
                                               uint32_t camera_buf_hes,
                                               uint32_t camera_buf_ves,
                                               size_t camera_buf_len);
// 打开设备并优先请求指定分辨率。
int app_video_open_preferred(char *dev,
                             video_fmt_t init_fmt,
                             uint32_t preferred_width,
                             uint32_t preferred_height);
// 注册用户态帧缓存。
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb);
// 返回当前协商到的单帧缓存大小。
uint32_t app_video_get_buf_size(void);
// 启动视频流读取任务。
esp_err_t app_video_stream_task_start(int video_fd, int core_id);
// 注册帧处理回调。
esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb);
#ifdef __cplusplus
}
#endif
#endif
