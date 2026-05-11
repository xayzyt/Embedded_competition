/*
 * SPDX-FileCopyrightText: 2026 OpenAI
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_camera.c 使用的视频设备底层封装，接口风格接近 V4L2。
 */
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
    /* 项目内部格式名到 Linux V4L2 像素格式常量的映射。 */
    APP_VIDEO_FMT_RAW8   = V4L2_PIX_FMT_SBGGR8,
    APP_VIDEO_FMT_RAW10  = V4L2_PIX_FMT_SBGGR10,
    APP_VIDEO_FMT_GREY   = V4L2_PIX_FMT_GREY,
    APP_VIDEO_FMT_RGB565 = V4L2_PIX_FMT_RGB565,
    APP_VIDEO_FMT_RGB888 = V4L2_PIX_FMT_RGB24,
    APP_VIDEO_FMT_YUV422 = V4L2_PIX_FMT_YUV422P,
    APP_VIDEO_FMT_YUV420 = V4L2_PIX_FMT_YUV420,
} video_fmt_t;

/* 摄像头每取到一帧后调用的处理回调类型。 */
typedef void (*app_video_frame_operation_cb_t)(uint8_t *camera_buf,
                                               uint8_t camera_buf_index,
                                               uint32_t camera_buf_hes,
                                               uint32_t camera_buf_ves,
                                               size_t camera_buf_len);

/* 打开并配置摄像头设备格式，返回视频设备 fd。 */
int app_video_open(char *dev, video_fmt_t init_fmt);

/* Prefer a concrete frame size, falling back to the sensor default if rejected. */
int app_video_open_preferred(char *dev,
                             video_fmt_t init_fmt,
                             uint32_t preferred_width,
                             uint32_t preferred_height);

/* 设置适合 AprilTag 识别的曝光和增益参数。 */
esp_err_t app_video_apply_recognition_profile(int video_fd, uint32_t exposure_us, uint8_t gain_percent);

/* 向 V4L2 驱动注册帧缓冲，可使用 USERPTR 或 MMAP。 */
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb);

/* 返回当前摄像头单帧缓冲大小。 */
uint32_t app_video_get_buf_size(void);

/* 启动视频流任务，循环取帧、回调处理并归还缓冲。 */
esp_err_t app_video_stream_task_start(int video_fd, int core_id);

/* 注册逐帧回调，调用点位于 DQBUF 和 QBUF 之间。 */
esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb);

#ifdef __cplusplus
}
#endif
#endif
