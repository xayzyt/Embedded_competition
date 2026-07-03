#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// 送达照片：接货瞬间抓取一帧，后台编码为 JPEG 并保存到 SD 卡。

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_DELIVERY_PHOTO_STATUS_NONE = 0,
    APP_DELIVERY_PHOTO_STATUS_CAPTURING,
    APP_DELIVERY_PHOTO_STATUS_READY,
    APP_DELIVERY_PHOTO_STATUS_UPLOADING,
    APP_DELIVERY_PHOTO_STATUS_UPLOADED,
    APP_DELIVERY_PHOTO_STATUS_FAILED,
} app_delivery_photo_status_t;

typedef struct {
    app_delivery_photo_status_t status;
    uint16_t target_id;
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint16_t chunks;
    uint16_t chunk_raw_size;
    char order_id[48];
    char request_id[32];
    char order_name[32];
    char photo_id[16];
    char file_path[96];
    char sha256_hex[65];
    char error[64];
} app_delivery_photo_info_t;

typedef void (*app_delivery_photo_status_cb_t)(void *user_ctx);

esp_err_t app_delivery_photo_init(void);
esp_err_t app_delivery_photo_register_status_callback(app_delivery_photo_status_cb_t cb,
                                                      void *user_ctx);
esp_err_t app_delivery_photo_begin_order(const char *order_id,
                                         const char *request_id,
                                         const char *order_name,
                                         uint16_t target_id);
esp_err_t app_delivery_photo_request_once(const char *trigger);
bool app_delivery_photo_should_capture_frame(void);
esp_err_t app_delivery_photo_submit_frame(const uint8_t *rgb565,
                                          uint32_t width,
                                          uint32_t height,
                                          size_t len);
bool app_delivery_photo_get_info(app_delivery_photo_info_t *out);
esp_err_t app_delivery_photo_read_jpeg_chunk(const char *photo_id,
                                             uint32_t offset,
                                             uint8_t *out,
                                             size_t out_size,
                                             size_t *out_len);
const char *app_delivery_photo_status_text(app_delivery_photo_status_t status);
esp_err_t app_delivery_photo_mark_uploading(const char *photo_id);
esp_err_t app_delivery_photo_mark_uploaded(const char *photo_id);
esp_err_t app_delivery_photo_mark_upload_retry(const char *photo_id, const char *error);

#ifdef __cplusplus
}
#endif
