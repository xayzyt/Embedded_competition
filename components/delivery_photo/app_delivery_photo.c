#include "app_delivery_photo.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "esp_jpeg_enc.h"
#include "mbedtls/sha256.h"
#include "app_image_utils.h"

// 送达照片只抓一帧。相机回调负责下采样复制，JPEG/SD 写入在后台任务里做。

#define DELIVERY_PHOTO_DIR              BSP_SD_MOUNT_POINT "/DPHOTO"
#define DELIVERY_PHOTO_WIDTH            160U
#define DELIVERY_PHOTO_HEIGHT           120U
#define DELIVERY_PHOTO_CHANNELS         3U
#define DELIVERY_PHOTO_RGB_BYTES        (DELIVERY_PHOTO_WIDTH * DELIVERY_PHOTO_HEIGHT * DELIVERY_PHOTO_CHANNELS)
#define DELIVERY_PHOTO_JPEG_MAX_BYTES   (64 * 1024)
#define DELIVERY_PHOTO_CHUNK_RAW_SIZE   384U
#define DELIVERY_PHOTO_QUEUE_LEN        1
#define DELIVERY_PHOTO_TASK_STACK       (7 * 1024)
#define DELIVERY_PHOTO_TASK_PRIO        3
#define DELIVERY_PHOTO_TASK_CORE        1
#define DELIVERY_PHOTO_MAX_INDEX        9999U
#define DELIVERY_PHOTO_MAX_CBS          2

typedef struct {
    char order_id[48];
    char request_id[32];
    char order_name[32];
    char photo_id[16];
    char file_path[96];
    uint16_t target_id;
    uint32_t generation;
} app_delivery_photo_job_t;

typedef struct {
    app_delivery_photo_status_cb_t cb;
    void *user_ctx;
} app_delivery_photo_cb_slot_t;

static const char *TAG = "delivery_photo";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_inited = false;
static bool s_storage_ready = false;
static bool s_capture_requested = false;
static bool s_worker_busy = false;
static uint32_t s_generation = 0;
static uint32_t s_next_index = 1;
static uint8_t *s_rgb888 = NULL;
static uint8_t *s_jpeg = NULL;
static QueueHandle_t s_job_queue = NULL;
static TaskHandle_t s_worker_task = NULL;
static app_delivery_photo_info_t s_info = {0};
static app_delivery_photo_cb_slot_t s_cbs[DELIVERY_PHOTO_MAX_CBS] = {0};

static void app_delivery_photo_emit_status(void)
{
    app_delivery_photo_cb_slot_t cbs[DELIVERY_PHOTO_MAX_CBS] = {0};
    taskENTER_CRITICAL(&s_mux);
    memcpy(cbs, s_cbs, sizeof(cbs));
    taskEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < DELIVERY_PHOTO_MAX_CBS; i++)
    {
        if (cbs[i].cb != NULL)
        {
            cbs[i].cb(cbs[i].user_ctx);
        }
    }
}

static bool app_delivery_photo_parse_file_name(const char *name, uint32_t *out_index)
{
    if (name == NULL || out_index == NULL || strlen(name) != 9U)
    {
        return false;
    }
    if (name[0] != 'P' || name[5] != '.' ||
        name[6] != 'J' || name[7] != 'P' || name[8] != 'G')
    {
        return false;
    }
    uint32_t value = 0;
    for (int i = 1; i < 5; i++)
    {
        if (name[i] < '0' || name[i] > '9')
        {
            return false;
        }
        value = (value * 10U) + (uint32_t)(name[i] - '0');
    }
    if (value == 0U || value > DELIVERY_PHOTO_MAX_INDEX)
    {
        return false;
    }
    *out_index = value;
    return true;
}

static uint32_t app_delivery_photo_find_next_index(void)
{
    uint32_t max_index = 0;
    DIR *dir = opendir(DELIVERY_PHOTO_DIR);
    if (dir == NULL)
    {
        return 1;
    }
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL)
    {
        uint32_t index = 0;
        if (app_delivery_photo_parse_file_name(ent->d_name, &index) && index > max_index)
        {
            max_index = index;
        }
    }
    closedir(dir);
    if (max_index >= DELIVERY_PHOTO_MAX_INDEX)
    {
        return DELIVERY_PHOTO_MAX_INDEX;
    }
    return max_index + 1U;
}

static esp_err_t app_delivery_photo_prepare_storage(void)
{
    if (s_storage_ready)
    {
        return ESP_OK;
    }
    esp_err_t ret = bsp_sdcard_mount();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ret = ESP_OK;
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "sd mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (mkdir(DELIVERY_PHOTO_DIR, 0775) != 0 && errno != EEXIST)
    {
        ESP_LOGW(TAG, "mkdir %s failed, errno=%d", DELIVERY_PHOTO_DIR, errno);
        return ESP_FAIL;
    }
    s_next_index = app_delivery_photo_find_next_index();
    s_storage_ready = true;
    return ESP_OK;
}

static uint32_t app_delivery_photo_take_index(void)
{
    uint32_t index = s_next_index;
    if (s_next_index < DELIVERY_PHOTO_MAX_INDEX)
    {
        s_next_index++;
    }
    return index;
}

static void app_delivery_photo_format_photo_path(uint32_t index,
                                                 char *photo_id,
                                                 size_t photo_id_size,
                                                 char *path,
                                                 size_t path_size)
{
    snprintf(photo_id, photo_id_size, "P%04lu", (unsigned long)index);
    snprintf(path, path_size, "%s/%s.JPG", DELIVERY_PHOTO_DIR, photo_id);
}

static void app_delivery_photo_set_failed_locked(const char *error)
{
    s_info.status = APP_DELIVERY_PHOTO_STATUS_FAILED;
    s_capture_requested = false;
    s_worker_busy = false;
    strlcpy(s_info.error, error != NULL ? error : "photo failed", sizeof(s_info.error));
}

static void app_delivery_photo_downsample_rgb565_to_rgb888(const uint8_t *rgb565,
                                                           uint32_t src_width,
                                                           uint32_t src_height,
                                                           uint8_t *dst_rgb)
{
    const uint16_t *src = (const uint16_t *)rgb565;
    uint32_t crop_x = 0;
    uint32_t crop_y = 0;
    uint32_t crop_w = src_width;
    uint32_t crop_h = src_height;
    app_image_calc_center_crop(src_width,
        src_height,
        DELIVERY_PHOTO_WIDTH,
        DELIVERY_PHOTO_HEIGHT,
        &crop_x,
        &crop_y,
        &crop_w,
        &crop_h);
    for (uint32_t y = 0; y < DELIVERY_PHOTO_HEIGHT; y++)
    {
        uint32_t sy = crop_y +
            (uint32_t)((((uint64_t)(2U * y + 1U)) * crop_h) / (2U * DELIVERY_PHOTO_HEIGHT));
        if (sy >= src_height)
        {
            sy = src_height - 1U;
        }
        const uint16_t *src_row = src + ((size_t)sy * src_width);
        uint8_t *dst_row = dst_rgb + ((size_t)y * DELIVERY_PHOTO_WIDTH * DELIVERY_PHOTO_CHANNELS);
        for (uint32_t x = 0; x < DELIVERY_PHOTO_WIDTH; x++)
        {
            uint32_t sx = crop_x +
                (uint32_t)((((uint64_t)(2U * x + 1U)) * crop_w) / (2U * DELIVERY_PHOTO_WIDTH));
            if (sx >= src_width)
            {
                sx = src_width - 1U;
            }
            uint16_t pixel = src_row[sx];
            uint8_t *p = dst_row + ((size_t)x * DELIVERY_PHOTO_CHANNELS);
            p[0] = app_image_rgb565_to_r(pixel);
            p[1] = app_image_rgb565_to_g(pixel);
            p[2] = app_image_rgb565_to_b(pixel);
        }
    }
}

static esp_err_t app_delivery_photo_write_file(const char *path, const uint8_t *data, size_t len)
{
    if (path == NULL || data == NULL || len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL)
    {
        ESP_LOGW(TAG, "open %s failed, errno=%d", path, errno);
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;
    if (fwrite(data, 1, len, file) != len)
    {
        ret = ESP_FAIL;
    }
    if (fclose(file) != 0 && ret == ESP_OK)
    {
        ret = ESP_FAIL;
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "write %s failed", path);
    }
    return ret;
}

static void app_delivery_photo_sha256_hex(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    if (out == NULL || out_size < 65U)
    {
        return;
    }
    uint8_t digest[32] = {0};
    if (mbedtls_sha256(data, len, digest, 0) != 0)
    {
        out[0] = '\0';
        return;
    }
    for (int i = 0; i < 32; i++)
    {
        snprintf(out + (i * 2), out_size - (size_t)(i * 2), "%02x", digest[i]);
    }
    out[64] = '\0';
}

static esp_err_t app_delivery_photo_encode_jpeg(int *out_len)
{
    if (out_len == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = DELIVERY_PHOTO_WIDTH;
    cfg.height = DELIVERY_PHOTO_HEIGHT;
    cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
    cfg.subsampling = JPEG_SUBSAMPLE_420;
    cfg.quality = 40;
    cfg.rotate = JPEG_ROTATE_0D;
    cfg.task_enable = false;

    jpeg_enc_handle_t handle = NULL;
    jpeg_error_t jret = jpeg_enc_open(&cfg, &handle);
    if (jret != JPEG_ERR_OK)
    {
        return ESP_FAIL;
    }
    jret = jpeg_enc_process(handle,
        s_rgb888,
        (int)DELIVERY_PHOTO_RGB_BYTES,
        s_jpeg,
        DELIVERY_PHOTO_JPEG_MAX_BYTES,
        out_len);
    (void)jpeg_enc_close(handle);
    return (jret == JPEG_ERR_OK && *out_len > 0) ? ESP_OK : ESP_FAIL;
}

static void app_delivery_photo_worker_task(void *arg)
{
    (void)arg;
    while (1)
    {
        app_delivery_photo_job_t job = {0};
        if (xQueueReceive(s_job_queue, &job, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        int jpeg_len = 0;
        esp_err_t ret = app_delivery_photo_encode_jpeg(&jpeg_len);
        esp_err_t storage_ret = app_delivery_photo_prepare_storage();
        if (ret == ESP_OK && storage_ret == ESP_OK)
        {
            storage_ret = app_delivery_photo_write_file(job.file_path, s_jpeg, (size_t)jpeg_len);
        }
        if (ret == ESP_OK && storage_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "delivery photo SD backup failed: %s", esp_err_to_name(storage_ret));
        }

        char sha256_hex[sizeof(s_info.sha256_hex)] = {0};
        if (ret == ESP_OK)
        {
            app_delivery_photo_sha256_hex(s_jpeg, (size_t)jpeg_len, sha256_hex, sizeof(sha256_hex));
        }

        bool stale_job = false;
        char log_path[sizeof(s_info.file_path)] = {0};
        uint32_t log_size = 0;
        uint16_t log_chunks = 0;
        taskENTER_CRITICAL(&s_mux);
        stale_job = job.generation != s_generation;
        if (!stale_job && ret == ESP_OK)
        {
            s_info.status = APP_DELIVERY_PHOTO_STATUS_READY;
            s_info.width = DELIVERY_PHOTO_WIDTH;
            s_info.height = DELIVERY_PHOTO_HEIGHT;
            s_info.size = (uint32_t)jpeg_len;
            s_info.chunk_raw_size = DELIVERY_PHOTO_CHUNK_RAW_SIZE;
            s_info.chunks = (uint16_t)(((uint32_t)jpeg_len + DELIVERY_PHOTO_CHUNK_RAW_SIZE - 1U) /
                DELIVERY_PHOTO_CHUNK_RAW_SIZE);
            s_info.error[0] = '\0';
            strlcpy(s_info.sha256_hex, sha256_hex, sizeof(s_info.sha256_hex));
            strlcpy(s_info.order_id, job.order_id, sizeof(s_info.order_id));
            strlcpy(s_info.request_id, job.request_id, sizeof(s_info.request_id));
            strlcpy(s_info.order_name, job.order_name, sizeof(s_info.order_name));
            strlcpy(s_info.photo_id, job.photo_id, sizeof(s_info.photo_id));
            strlcpy(s_info.file_path, job.file_path, sizeof(s_info.file_path));
            s_info.target_id = job.target_id;
            strlcpy(log_path, s_info.file_path, sizeof(log_path));
            log_size = s_info.size;
            log_chunks = s_info.chunks;
        }
        else if (!stale_job)
        {
            app_delivery_photo_set_failed_locked(esp_err_to_name(ret));
        }
        s_worker_busy = false;
        taskEXIT_CRITICAL(&s_mux);
        if (stale_job)
        {
            ESP_LOGW(TAG, "discard stale delivery photo job: %s", job.photo_id);
        }
        else if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "delivery photo ready: %s size=%lu chunks=%u",
                log_path,
                (unsigned long)log_size,
                (unsigned)log_chunks);
            app_delivery_photo_emit_status();
        }
        else
        {
            ESP_LOGW(TAG, "delivery photo failed: %s", esp_err_to_name(ret));
            app_delivery_photo_emit_status();
        }
    }
}

esp_err_t app_delivery_photo_init(void)
{
    if (s_inited)
    {
        return ESP_OK;
    }
    s_rgb888 = heap_caps_aligned_alloc(16,
        DELIVERY_PHOTO_RGB_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_jpeg = heap_caps_aligned_alloc(16,
        DELIVERY_PHOTO_JPEG_MAX_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rgb888 == NULL || s_jpeg == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    s_job_queue = xQueueCreate(DELIVERY_PHOTO_QUEUE_LEN, sizeof(app_delivery_photo_job_t));
    if (s_job_queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(app_delivery_photo_worker_task,
        "delivery_photo",
        DELIVERY_PHOTO_TASK_STACK,
        NULL,
        DELIVERY_PHOTO_TASK_PRIO,
        &s_worker_task,
        DELIVERY_PHOTO_TASK_CORE);
    if (ok != pdPASS)
    {
        s_worker_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    (void)app_delivery_photo_prepare_storage();
    taskENTER_CRITICAL(&s_mux);
    memset(&s_info, 0, sizeof(s_info));
    s_info.status = APP_DELIVERY_PHOTO_STATUS_NONE;
    s_info.width = DELIVERY_PHOTO_WIDTH;
    s_info.height = DELIVERY_PHOTO_HEIGHT;
    s_info.chunk_raw_size = DELIVERY_PHOTO_CHUNK_RAW_SIZE;
    s_generation = 0;
    s_inited = true;
    taskEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

esp_err_t app_delivery_photo_register_status_callback(app_delivery_photo_status_cb_t cb,
                                                      void *user_ctx)
{
    if (cb == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < DELIVERY_PHOTO_MAX_CBS; i++)
    {
        if (s_cbs[i].cb == NULL)
        {
            s_cbs[i].cb = cb;
            s_cbs[i].user_ctx = user_ctx;
            taskEXIT_CRITICAL(&s_mux);
            return ESP_OK;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    return ESP_ERR_NO_MEM;
}

esp_err_t app_delivery_photo_begin_order(const char *order_id,
                                         const char *request_id,
                                         const char *order_name,
                                         uint16_t target_id,
                                         uint32_t task_generation)
{
    if (!s_inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t index = app_delivery_photo_take_index();
    char photo_id[sizeof(s_info.photo_id)] = {0};
    char file_path[sizeof(s_info.file_path)] = {0};
    app_delivery_photo_format_photo_path(index,
        photo_id,
        sizeof(photo_id),
        file_path,
        sizeof(file_path));

    taskENTER_CRITICAL(&s_mux);
    memset(&s_info, 0, sizeof(s_info));
    s_info.status = APP_DELIVERY_PHOTO_STATUS_NONE;
    s_info.task_generation = task_generation;
    s_info.target_id = target_id;
    s_info.width = DELIVERY_PHOTO_WIDTH;
    s_info.height = DELIVERY_PHOTO_HEIGHT;
    s_info.chunk_raw_size = DELIVERY_PHOTO_CHUNK_RAW_SIZE;
    s_generation++;
    strlcpy(s_info.order_id, order_id != NULL ? order_id : "", sizeof(s_info.order_id));
    strlcpy(s_info.request_id, request_id != NULL ? request_id : "", sizeof(s_info.request_id));
    strlcpy(s_info.order_name, order_name != NULL ? order_name : "", sizeof(s_info.order_name));
    strlcpy(s_info.photo_id, photo_id, sizeof(s_info.photo_id));
    strlcpy(s_info.file_path, file_path, sizeof(s_info.file_path));
    s_capture_requested = false;
    taskEXIT_CRITICAL(&s_mux);
    app_delivery_photo_emit_status();
    return ESP_OK;
}

esp_err_t app_delivery_photo_request_once(const char *trigger)
{
    if (!s_inited)
    {
        return ESP_ERR_INVALID_STATE;
    }
    bool changed = false;
    taskENTER_CRITICAL(&s_mux);
    if (s_info.order_id[0] == '\0' || s_info.photo_id[0] == '\0')
    {
        taskEXIT_CRITICAL(&s_mux);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_info.status == APP_DELIVERY_PHOTO_STATUS_NONE)
    {
        s_info.status = APP_DELIVERY_PHOTO_STATUS_CAPTURING;
        s_capture_requested = true;
        s_info.error[0] = '\0';
        changed = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (changed)
    {
        ESP_LOGI(TAG, "delivery photo requested: %s", trigger != NULL ? trigger : "-");
        app_delivery_photo_emit_status();
    }
    return ESP_OK;
}

bool app_delivery_photo_should_capture_frame(void)
{
    bool should = false;
    taskENTER_CRITICAL(&s_mux);
    should = s_inited &&
        s_capture_requested &&
        !s_worker_busy &&
        s_info.status == APP_DELIVERY_PHOTO_STATUS_CAPTURING;
    taskEXIT_CRITICAL(&s_mux);
    return should;
}

esp_err_t app_delivery_photo_submit_frame(const uint8_t *rgb565,
                                          uint32_t width,
                                          uint32_t height,
                                          size_t len)
{
    if (rgb565 == NULL || width == 0U || height == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len < ((size_t)width * (size_t)height * 2U))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    app_delivery_photo_job_t job = {0};
    taskENTER_CRITICAL(&s_mux);
    if (!s_inited || !s_capture_requested || s_worker_busy ||
        s_info.status != APP_DELIVERY_PHOTO_STATUS_CAPTURING)
    {
        taskEXIT_CRITICAL(&s_mux);
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(job.order_id, s_info.order_id, sizeof(job.order_id));
    strlcpy(job.request_id, s_info.request_id, sizeof(job.request_id));
    strlcpy(job.order_name, s_info.order_name, sizeof(job.order_name));
    strlcpy(job.photo_id, s_info.photo_id, sizeof(job.photo_id));
    strlcpy(job.file_path, s_info.file_path, sizeof(job.file_path));
    job.target_id = s_info.target_id;
    job.generation = s_generation;
    s_capture_requested = false;
    s_worker_busy = true;
    taskEXIT_CRITICAL(&s_mux);

    app_delivery_photo_downsample_rgb565_to_rgb888(rgb565, width, height, s_rgb888);
    if (xQueueSend(s_job_queue, &job, 0) != pdTRUE)
    {
        taskENTER_CRITICAL(&s_mux);
        app_delivery_photo_set_failed_locked("queue full");
        taskEXIT_CRITICAL(&s_mux);
        app_delivery_photo_emit_status();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool app_delivery_photo_get_info(app_delivery_photo_info_t *out)
{
    if (out == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    *out = s_info;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

esp_err_t app_delivery_photo_read_jpeg_chunk(const char *photo_id,
                                             uint32_t offset,
                                             uint8_t *out,
                                             size_t out_size,
                                             size_t *out_len)
{
    if (photo_id == NULL || out == NULL || out_size == 0U || out_len == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_mux);
    const bool readable =
        s_jpeg != NULL &&
        strcmp(photo_id, s_info.photo_id) == 0 &&
        (s_info.status == APP_DELIVERY_PHOTO_STATUS_READY ||
            s_info.status == APP_DELIVERY_PHOTO_STATUS_UPLOADING ||
            s_info.status == APP_DELIVERY_PHOTO_STATUS_UPLOADED) &&
        offset < s_info.size;
    if (!readable)
    {
        taskEXIT_CRITICAL(&s_mux);
        *out_len = 0;
        return ESP_ERR_NOT_FOUND;
    }

    size_t copy_len = (size_t)(s_info.size - offset);
    if (copy_len > out_size)
    {
        copy_len = out_size;
    }
    memcpy(out, s_jpeg + offset, copy_len);
    taskEXIT_CRITICAL(&s_mux);
    *out_len = copy_len;
    return ESP_OK;
}

const char *app_delivery_photo_status_text(app_delivery_photo_status_t status)
{
    switch (status)
    {
    case APP_DELIVERY_PHOTO_STATUS_CAPTURING:
        return "capturing";
    case APP_DELIVERY_PHOTO_STATUS_READY:
        return "ready";
    case APP_DELIVERY_PHOTO_STATUS_UPLOADING:
        return "uploading";
    case APP_DELIVERY_PHOTO_STATUS_UPLOADED:
        return "uploaded";
    case APP_DELIVERY_PHOTO_STATUS_FAILED:
        return "failed";
    case APP_DELIVERY_PHOTO_STATUS_NONE:
    default:
        return "none";
    }
}

esp_err_t app_delivery_photo_mark_uploading(const char *photo_id)
{
    bool changed = false;
    taskENTER_CRITICAL(&s_mux);
    if (photo_id == NULL || strcmp(photo_id, s_info.photo_id) != 0 ||
        s_info.status != APP_DELIVERY_PHOTO_STATUS_READY)
    {
        taskEXIT_CRITICAL(&s_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_info.status = APP_DELIVERY_PHOTO_STATUS_UPLOADING;
    s_info.error[0] = '\0';
    changed = true;
    taskEXIT_CRITICAL(&s_mux);
    if (changed)
    {
        app_delivery_photo_emit_status();
    }
    return ESP_OK;
}

esp_err_t app_delivery_photo_mark_uploaded(const char *photo_id)
{
    bool changed = false;
    taskENTER_CRITICAL(&s_mux);
    if (photo_id == NULL || strcmp(photo_id, s_info.photo_id) != 0 ||
        s_info.status != APP_DELIVERY_PHOTO_STATUS_UPLOADING)
    {
        taskEXIT_CRITICAL(&s_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_info.status = APP_DELIVERY_PHOTO_STATUS_UPLOADED;
    s_info.error[0] = '\0';
    changed = true;
    taskEXIT_CRITICAL(&s_mux);
    if (changed)
    {
        app_delivery_photo_emit_status();
    }
    return ESP_OK;
}

esp_err_t app_delivery_photo_mark_upload_retry(const char *photo_id, const char *error)
{
    taskENTER_CRITICAL(&s_mux);
    if (photo_id == NULL || strcmp(photo_id, s_info.photo_id) != 0 ||
        s_info.status != APP_DELIVERY_PHOTO_STATUS_UPLOADING)
    {
        taskEXIT_CRITICAL(&s_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_info.status = APP_DELIVERY_PHOTO_STATUS_READY;
    strlcpy(s_info.error, error != NULL ? error : "upload retry", sizeof(s_info.error));
    taskEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}
