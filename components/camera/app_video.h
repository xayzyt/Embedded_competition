#ifndef APP_VIDEO_H
#define APP_VIDEO_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "linux/videodev2.h"
#include "esp_video_device.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    APP_VIDEO_FMT_RAW8   = V4L2_PIX_FMT_SBGGR8,
    APP_VIDEO_FMT_RAW10  = V4L2_PIX_FMT_SBGGR10,
    APP_VIDEO_FMT_GREY   = V4L2_PIX_FMT_GREY,
    APP_VIDEO_FMT_RGB565 = V4L2_PIX_FMT_RGB565,
    APP_VIDEO_FMT_RGB888 = V4L2_PIX_FMT_RGB24,
    APP_VIDEO_FMT_YUV422 = V4L2_PIX_FMT_YUV422P,
    APP_VIDEO_FMT_YUV420 = V4L2_PIX_FMT_YUV420,
} video_fmt_t;
typedef void (*app_video_frame_operation_cb_t)(uint8_t *camera_buf,
                                               uint8_t camera_buf_index,
                                               uint32_t camera_buf_hes,
                                               uint32_t camera_buf_ves,
                                               size_t camera_buf_len);
int app_video_open(char *dev, video_fmt_t init_fmt);
int app_video_open_preferred(char *dev,
                             video_fmt_t init_fmt,
                             uint32_t preferred_width,
                             uint32_t preferred_height);
esp_err_t app_video_apply_recognition_profile(int video_fd, uint32_t exposure_us, uint8_t gain_percent);
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb);
uint32_t app_video_get_buf_size(void);
esp_err_t app_video_stream_task_start(int video_fd, int core_id);
esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb);
#ifdef __cplusplus
}
#endif
#endif
