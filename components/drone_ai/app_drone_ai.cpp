#include "app_drone_ai.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fbs_loader.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

extern "C" {
#include "app_ui.h"
}

static const char *TAG = "drone_ai";

#define DRONE_AI_MODEL_PARTITION_LABEL "model"
#define DRONE_AI_INPUT_W              128U
#define DRONE_AI_INPUT_H              128U
#define DRONE_AI_INPUT_CH             3U
#define DRONE_AI_INPUT_PIXELS         (DRONE_AI_INPUT_W * DRONE_AI_INPUT_H)
#define DRONE_AI_RGB_BYTES            (DRONE_AI_INPUT_PIXELS * DRONE_AI_INPUT_CH)
#define DRONE_AI_SLOT_COUNT           2U
#define DRONE_AI_TASK_STACK_SIZE      (24 * 1024)
#define DRONE_AI_TASK_PRIORITY        0
#define DRONE_AI_TASK_CORE_ID         1
#define DRONE_AI_THRESHOLD            0.70f
#define DRONE_AI_CONFIRM_HITS         3U
#define DRONE_AI_MALLOC_PSRAM_LIMIT   0U
#define DRONE_AI_STATUS_INTERVAL_MS   1000U
#define DRONE_AI_LOG_INTERVAL         10U

typedef struct {
    uint8_t *rgb;
    uint32_t src_width;
    uint32_t src_height;
    uint32_t seq;
    uint32_t tick_ms;
} app_drone_ai_slot_t;

static portMUX_TYPE s_ai_mux = portMUX_INITIALIZER_UNLOCKED;
static app_drone_ai_slot_t s_slots[DRONE_AI_SLOT_COUNT] = {};
static QueueHandle_t s_free_queue = NULL;
static QueueHandle_t s_infer_queue = NULL;
static TaskHandle_t s_ai_task = NULL;
static bool s_inited = false;
static bool s_model_load_requested = false;
static bool s_model_ready = false;
static esp_err_t s_model_status = ESP_ERR_INVALID_STATE;
static bool s_confirmed = false;
static uint8_t s_hit_count = 0;
static uint32_t s_submit_seq = 0;
static uint32_t s_infer_count = 0;
static uint32_t s_drop_count = 0;
static uint32_t s_last_status_ms = 0;
static app_drone_ai_result_t s_latest = {};

static dl::Model *s_model = nullptr;
static dl::TensorBase *s_model_input = nullptr;
static dl::TensorBase *s_model_output = nullptr;
static fbs::FbsLoader *s_fbs_loader = nullptr;
static fbs::FbsModel *s_fbs_model = nullptr;

static void app_drone_ai_restore_default_malloc(void)
{
#if defined(CONFIG_SPIRAM_USE_MALLOC) && CONFIG_SPIRAM_USE_MALLOC
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#else
    heap_caps_malloc_extmem_enable((size_t)-1);
#endif
}

class app_drone_ai_malloc_psram_scope_t {
public:
    app_drone_ai_malloc_psram_scope_t()
    {
        heap_caps_malloc_extmem_enable(DRONE_AI_MALLOC_PSRAM_LIMIT);
    }

    ~app_drone_ai_malloc_psram_scope_t()
    {
        app_drone_ai_restore_default_malloc();
    }

    app_drone_ai_malloc_psram_scope_t(const app_drone_ai_malloc_psram_scope_t &) = delete;
    app_drone_ai_malloc_psram_scope_t &operator=(const app_drone_ai_malloc_psram_scope_t &) = delete;
};

static inline uint32_t app_drone_ai_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static inline float app_drone_ai_imagenet_norm(uint8_t value, uint32_t channel)
{
    static const float mean[DRONE_AI_INPUT_CH] = {0.485f, 0.456f, 0.406f};
    static const float stddev[DRONE_AI_INPUT_CH] = {0.229f, 0.224f, 0.225f};
    return (((float)value / 255.0f) - mean[channel]) / stddev[channel];
}

static inline int8_t app_drone_ai_quant_i8(float value, int exponent)
{
    const float inv_scale = ldexpf(1.0f, -exponent);
    int32_t q = (int32_t)lrintf(value * inv_scale);
    if (q < -128)
    {
        q = -128;
    }
    if (q > 127)
    {
        q = 127;
    }
    return (int8_t)q;
}

static inline int16_t app_drone_ai_quant_i16(float value, int exponent)
{
    const float inv_scale = ldexpf(1.0f, -exponent);
    int32_t q = (int32_t)lrintf(value * inv_scale);
    if (q < -32768)
    {
        q = -32768;
    }
    if (q > 32767)
    {
        q = 32767;
    }
    return (int16_t)q;
}

static inline float app_drone_ai_dequant_i8(int8_t value, int exponent)
{
    return (float)value * ldexpf(1.0f, exponent);
}

static inline float app_drone_ai_dequant_i16(int16_t value, int exponent)
{
    return (float)value * ldexpf(1.0f, exponent);
}

static inline void app_drone_ai_rgb565_to_rgb888(uint16_t pixel,
                                                 uint8_t *r,
                                                 uint8_t *g,
                                                 uint8_t *b)
{
    uint32_t rv = (pixel >> 11) & 0x1FU;
    uint32_t gv = (pixel >> 5) & 0x3FU;
    uint32_t bv = pixel & 0x1FU;

    rv = (rv << 3) | (rv >> 2);
    gv = (gv << 2) | (gv >> 4);
    bv = (bv << 3) | (bv >> 2);

    *r = (uint8_t)rv;
    *g = (uint8_t)gv;
    *b = (uint8_t)bv;
}

static void app_drone_ai_calc_center_crop(uint32_t src_width,
                                          uint32_t src_height,
                                          uint32_t dst_width,
                                          uint32_t dst_height,
                                          uint32_t *crop_x,
                                          uint32_t *crop_y,
                                          uint32_t *crop_w,
                                          uint32_t *crop_h)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = src_width;
    uint32_t h = src_height;

    if (src_width == 0U || src_height == 0U || dst_width == 0U || dst_height == 0U)
    {
        if (crop_x) *crop_x = 0;
        if (crop_y) *crop_y = 0;
        if (crop_w) *crop_w = src_width;
        if (crop_h) *crop_h = src_height;
        return;
    }

    if ((uint64_t)src_width * dst_height > (uint64_t)src_height * dst_width)
    {
        w = (uint32_t)(((uint64_t)src_height * dst_width) / dst_height);
        x = (src_width - w) / 2U;
    }
    else
    {
        h = (uint32_t)(((uint64_t)src_width * dst_height) / dst_width);
        y = (src_height - h) / 2U;
    }

    if (crop_x) *crop_x = x;
    if (crop_y) *crop_y = y;
    if (crop_w) *crop_w = w;
    if (crop_h) *crop_h = h;
}

static void app_drone_ai_log_heap(const char *stage)
{
    const UBaseType_t stack_free_words = uxTaskGetStackHighWaterMark(NULL);

    ESP_LOGI(TAG,
             "%s heap: int_free=%lu int_largest=%lu psram_free=%lu psram_largest=%lu stack_free=%lu",
             stage,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned long)(stack_free_words * sizeof(StackType_t)));
}

static bool app_drone_ai_input_is_nhwc(const std::vector<int> &shape)
{
    return shape.size() == 4U &&
           shape[1] == (int)DRONE_AI_INPUT_H &&
           shape[2] == (int)DRONE_AI_INPUT_W &&
           shape[3] == (int)DRONE_AI_INPUT_CH;
}

static bool app_drone_ai_input_is_nchw(const std::vector<int> &shape)
{
    return shape.size() == 4U &&
           shape[1] == (int)DRONE_AI_INPUT_CH &&
           shape[2] == (int)DRONE_AI_INPUT_H &&
           shape[3] == (int)DRONE_AI_INPUT_W;
}

static inline size_t app_drone_ai_input_offset(bool is_nhwc,
                                               uint32_t y,
                                               uint32_t x,
                                               uint32_t c)
{
    if (is_nhwc)
    {
        return ((size_t)y * DRONE_AI_INPUT_W + x) * DRONE_AI_INPUT_CH + c;
    }

    return (size_t)c * DRONE_AI_INPUT_PIXELS + (size_t)y * DRONE_AI_INPUT_W + x;
}

static void app_drone_ai_release_model_objects(void)
{
    if (s_model != nullptr)
    {
        delete s_model;
        s_model = nullptr;
    }
    if (s_fbs_model != nullptr)
    {
        delete s_fbs_model;
        s_fbs_model = nullptr;
    }
    if (s_fbs_loader != nullptr)
    {
        delete s_fbs_loader;
        s_fbs_loader = nullptr;
    }
    s_model_input = nullptr;
    s_model_output = nullptr;
}

static void app_drone_ai_resize_rgb565_to_rgb888(const uint8_t *rgb565,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint8_t *dst_rgb)
{
    uint32_t crop_x = 0;
    uint32_t crop_y = 0;
    uint32_t crop_w = width;
    uint32_t crop_h = height;
    const uint16_t *src = (const uint16_t *)rgb565;

    app_drone_ai_calc_center_crop(width,
                                  height,
                                  DRONE_AI_INPUT_W,
                                  DRONE_AI_INPUT_H,
                                  &crop_x,
                                  &crop_y,
                                  &crop_w,
                                  &crop_h);

    for (uint32_t y = 0; y < DRONE_AI_INPUT_H; y++) {
        uint32_t sy = crop_y + (uint32_t)(((uint64_t)y * crop_h) / DRONE_AI_INPUT_H);
        if (sy >= height)
        {
            sy = height - 1U;
        }
        const uint16_t *src_row = src + (size_t)sy * width;
        uint8_t *dst_row = dst_rgb + (size_t)y * DRONE_AI_INPUT_W * DRONE_AI_INPUT_CH;

        for (uint32_t x = 0; x < DRONE_AI_INPUT_W; x++) {
            uint32_t sx = crop_x + (uint32_t)(((uint64_t)x * crop_w) / DRONE_AI_INPUT_W);
            if (sx >= width)
            {
                sx = width - 1U;
            }

            app_drone_ai_rgb565_to_rgb888(src_row[sx],
                                          &dst_row[x * DRONE_AI_INPUT_CH + 0U],
                                          &dst_row[x * DRONE_AI_INPUT_CH + 1U],
                                          &dst_row[x * DRONE_AI_INPUT_CH + 2U]);
        }
    }
}

static esp_err_t app_drone_ai_load_model(void)
{
    if (s_model != nullptr)
    {
        return ESP_OK;
    }

    const app_drone_ai_malloc_psram_scope_t malloc_scope;
    const int64_t load_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "model load: default malloc temporarily prefers PSRAM");
    app_drone_ai_log_heap("before fbs loader");

    ESP_LOGI(TAG, "model load step 1/3: create fbs loader partition=%s", DRONE_AI_MODEL_PARTITION_LABEL);
    s_fbs_loader = new (std::nothrow) fbs::FbsLoader(DRONE_AI_MODEL_PARTITION_LABEL,
                                                     fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
    if (s_fbs_loader == nullptr)
    {
        ESP_LOGE(TAG, "fbs loader alloc failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "model load step 1/3 done in %lldms", (esp_timer_get_time() - load_start_us) / 1000LL);
    app_drone_ai_log_heap("after fbs loader");

    const int64_t fbs_load_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "model load step 2/3: parse fbs model");
    s_fbs_model = s_fbs_loader->load(nullptr, false);
    if (s_fbs_model == nullptr)
    {
        ESP_LOGE(TAG, "fbs model load failed");
        app_drone_ai_release_model_objects();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "model load step 2/3 done in %lldms", (esp_timer_get_time() - fbs_load_start_us) / 1000LL);
    app_drone_ai_log_heap("after fbs model");

    const int64_t build_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "model load step 3/3: build dl model");
    s_model = new (std::nothrow) dl::Model(s_fbs_model, 0, dl::MEMORY_MANAGER_GREEDY);
    if (s_model == nullptr)
    {
        ESP_LOGE(TAG, "dl model alloc failed");
        app_drone_ai_release_model_objects();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "model load step 3/3 done in %lldms", (esp_timer_get_time() - build_start_us) / 1000LL);
    app_drone_ai_log_heap("after dl model");

    std::map<std::string, dl::TensorBase *> &inputs = s_model->get_inputs();
    std::map<std::string, dl::TensorBase *> &outputs = s_model->get_outputs();
    if (inputs.empty() || outputs.empty())
    {
        ESP_LOGE(TAG, "model input/output missing");
        app_drone_ai_release_model_objects();
        return ESP_FAIL;
    }

    s_model_input = inputs.begin()->second;
    s_model_output = outputs.begin()->second;
    ESP_LOGI(TAG,
             "model ready: input dtype=%s exp=%d bytes=%d shape=%dx%dx%dx%d output dtype=%s exp=%d size=%d",
             s_model_input->get_dtype_string(),
             s_model_input->exponent.get(),
             s_model_input->get_bytes(),
             s_model_input->shape.size() > 0U ? s_model_input->shape[0] : 0,
             s_model_input->shape.size() > 1U ? s_model_input->shape[1] : 0,
             s_model_input->shape.size() > 2U ? s_model_input->shape[2] : 0,
             s_model_input->shape.size() > 3U ? s_model_input->shape[3] : 0,
             s_model_output->get_dtype_string(),
             s_model_output->exponent.get(),
             s_model_output->get_size());
    ESP_LOGI(TAG, "model load total %lldms", (esp_timer_get_time() - load_start_us) / 1000LL);
    app_ui_set_capture_text("ai: model ready");
    return ESP_OK;
}

static void app_drone_ai_set_model_status(esp_err_t status)
{
    taskENTER_CRITICAL(&s_ai_mux);
    s_model_status = status;
    s_model_ready = (status == ESP_OK);
    taskEXIT_CRITICAL(&s_ai_mux);
}

static esp_err_t app_drone_ai_fill_input(const uint8_t *rgb)
{
    if (s_model_input == nullptr || rgb == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const int total = s_model_input->get_size();
    if (total < (int)(DRONE_AI_INPUT_PIXELS * DRONE_AI_INPUT_CH))
    {
        ESP_LOGE(TAG, "model input too small: %d", total);
        return ESP_ERR_INVALID_SIZE;
    }

    const std::vector<int> &shape = s_model_input->shape;
    const bool is_nhwc = app_drone_ai_input_is_nhwc(shape);
    if (!is_nhwc && !app_drone_ai_input_is_nchw(shape))
    {
        ESP_LOGW(TAG, "unexpected input shape, fallback to NCHW fill");
    }

    if (s_model_input->dtype == dl::DATA_TYPE_INT8)
    {
        int8_t *dst = s_model_input->get_element_ptr<int8_t>();
        for (uint32_t y = 0; y < DRONE_AI_INPUT_H; y++) {
            for (uint32_t x = 0; x < DRONE_AI_INPUT_W; x++) {
                const size_t rgb_offset = ((size_t)y * DRONE_AI_INPUT_W + x) * DRONE_AI_INPUT_CH;
                for (uint32_t c = 0; c < DRONE_AI_INPUT_CH; c++) {
                    const int exponent = s_model_input->exponent.get((int)c);
                    const float norm = app_drone_ai_imagenet_norm(rgb[rgb_offset + c], c);
                    dst[app_drone_ai_input_offset(is_nhwc, y, x, c)] = app_drone_ai_quant_i8(norm, exponent);
                }
            }
        }
        return ESP_OK;
    }

    if (s_model_input->dtype == dl::DATA_TYPE_INT16)
    {
        int16_t *dst = s_model_input->get_element_ptr<int16_t>();
        for (uint32_t y = 0; y < DRONE_AI_INPUT_H; y++) {
            for (uint32_t x = 0; x < DRONE_AI_INPUT_W; x++) {
                const size_t rgb_offset = ((size_t)y * DRONE_AI_INPUT_W + x) * DRONE_AI_INPUT_CH;
                for (uint32_t c = 0; c < DRONE_AI_INPUT_CH; c++) {
                    const int exponent = s_model_input->exponent.get((int)c);
                    const float norm = app_drone_ai_imagenet_norm(rgb[rgb_offset + c], c);
                    dst[app_drone_ai_input_offset(is_nhwc, y, x, c)] = app_drone_ai_quant_i16(norm, exponent);
                }
            }
        }
        return ESP_OK;
    }

    if (s_model_input->dtype == dl::DATA_TYPE_FLOAT)
    {
        float *dst = s_model_input->get_element_ptr<float>();
        for (uint32_t y = 0; y < DRONE_AI_INPUT_H; y++) {
            for (uint32_t x = 0; x < DRONE_AI_INPUT_W; x++) {
                const size_t rgb_offset = ((size_t)y * DRONE_AI_INPUT_W + x) * DRONE_AI_INPUT_CH;
                for (uint32_t c = 0; c < DRONE_AI_INPUT_CH; c++) {
                    dst[app_drone_ai_input_offset(is_nhwc, y, x, c)] =
                        app_drone_ai_imagenet_norm(rgb[rgb_offset + c], c);
                }
            }
        }
        return ESP_OK;
    }

    ESP_LOGE(TAG, "unsupported input dtype: %s", s_model_input->get_dtype_string());
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t app_drone_ai_read_logits(float logits[2])
{
    if (s_model_output == nullptr || logits == nullptr || s_model_output->get_size() < 2)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_model_output->dtype == dl::DATA_TYPE_INT8)
    {
        int8_t *out = s_model_output->get_element_ptr<int8_t>();
        logits[0] = app_drone_ai_dequant_i8(out[0], s_model_output->exponent.get(0));
        logits[1] = app_drone_ai_dequant_i8(out[1], s_model_output->exponent.get(1));
        return ESP_OK;
    }

    if (s_model_output->dtype == dl::DATA_TYPE_INT16)
    {
        int16_t *out = s_model_output->get_element_ptr<int16_t>();
        logits[0] = app_drone_ai_dequant_i16(out[0], s_model_output->exponent.get(0));
        logits[1] = app_drone_ai_dequant_i16(out[1], s_model_output->exponent.get(1));
        return ESP_OK;
    }

    if (s_model_output->dtype == dl::DATA_TYPE_FLOAT)
    {
        float *out = s_model_output->get_element_ptr<float>();
        logits[0] = out[0];
        logits[1] = out[1];
        return ESP_OK;
    }

    ESP_LOGE(TAG, "unsupported output dtype: %s", s_model_output->get_dtype_string());
    return ESP_ERR_NOT_SUPPORTED;
}

static void app_drone_ai_softmax2(const float logits[2], float *nodrone, float *drone)
{
    const float m = std::max(logits[0], logits[1]);
    const float e0 = expf(logits[0] - m);
    const float e1 = expf(logits[1] - m);
    const float denom = e0 + e1;

    if (denom <= 0.0f)
    {
        *nodrone = 0.5f;
        *drone = 0.5f;
        return;
    }

    *nodrone = e0 / denom;
    *drone = e1 / denom;
}

static void app_drone_ai_update_result(uint32_t frame_seq,
                                       uint32_t infer_ms,
                                       float nodrone_score,
                                       float drone_score)
{
    app_drone_ai_result_t latest = {};

    latest.valid = true;
    latest.label = (drone_score >= nodrone_score) ? APP_DRONE_AI_CLASS_DRONE : APP_DRONE_AI_CLASS_NODRONE;
    latest.nodrone_score = nodrone_score;
    latest.drone_score = drone_score;
    latest.frame_seq = frame_seq;
    latest.infer_ms = infer_ms;

    taskENTER_CRITICAL(&s_ai_mux);
    if (!s_confirmed)
    {
        if (latest.label == APP_DRONE_AI_CLASS_DRONE && drone_score >= DRONE_AI_THRESHOLD)
        {
            if (s_hit_count < 255U)
            {
                s_hit_count++;
            }
            if (s_hit_count >= DRONE_AI_CONFIRM_HITS)
            {
                s_confirmed = true;
            }
        }
        else
        {
            s_hit_count = 0;
        }
    }
    latest.hit_count = s_hit_count;
    latest.confirmed = s_confirmed;
    s_latest = latest;
    taskEXIT_CRITICAL(&s_ai_mux);
}

static void app_drone_ai_publish_status(void)
{
    char text[64] = {};
    app_drone_ai_format_status(text, sizeof(text));
    app_ui_set_capture_text(text);
}

static void app_drone_ai_task(void *arg)
{
    (void)arg;

    app_ui_set_capture_text("ai: idle");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    app_ui_set_capture_text("ai: loading model");
    while (1) {
        esp_err_t load_ret = app_drone_ai_load_model();
        app_drone_ai_set_model_status(load_ret);
        if (load_ret == ESP_OK)
        {
            break;
        }
        app_ui_set_capture_text("ai: model load fail");
        ESP_LOGW(TAG, "model load failed: %s", esp_err_to_name(load_ret));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (1) {
        app_drone_ai_slot_t *slot = NULL;
        if (xQueueReceive(s_infer_queue, &slot, portMAX_DELAY) != pdTRUE || slot == NULL)
        {
            continue;
        }

        esp_err_t ret = app_drone_ai_fill_input(slot->rgb);
        if (ret == ESP_OK)
        {
            const int64_t start_us = esp_timer_get_time();
            s_model->run();
            const uint32_t infer_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000ULL);
            float logits[2] = {0.0f, 0.0f};

            ret = app_drone_ai_read_logits(logits);
            if (ret == ESP_OK)
            {
                float nodrone_score = 0.0f;
                float drone_score = 0.0f;
                app_drone_ai_softmax2(logits, &nodrone_score, &drone_score);
                app_drone_ai_update_result(slot->seq, infer_ms, nodrone_score, drone_score);

                const uint32_t now_ms = app_drone_ai_now_ms();
                uint32_t infer_count = 0;
                taskENTER_CRITICAL(&s_ai_mux);
                s_infer_count++;
                infer_count = s_infer_count;
                taskEXIT_CRITICAL(&s_ai_mux);
                const bool candidate_hit = (drone_score >= DRONE_AI_THRESHOLD);
                if (candidate_hit || (now_ms - s_last_status_ms) >= DRONE_AI_STATUS_INTERVAL_MS)
                {
                    app_drone_ai_publish_status();
                    s_last_status_ms = now_ms;
                }

                if (candidate_hit || (infer_count % DRONE_AI_LOG_INTERVAL) == 0U)
                {
                    ESP_LOGI(TAG,
                             "infer %lums label=%s nodrone=%.3f drone=%.3f hits=%u/%u confirmed=%d",
                             (unsigned long)infer_ms,
                             drone_score >= nodrone_score ? "DRONE" : "NODRONE",
                             (double)nodrone_score,
                             (double)drone_score,
                             (unsigned)s_hit_count,
                             (unsigned)DRONE_AI_CONFIRM_HITS,
                             (int)s_confirmed);
                }
            }
            else
            {
                ESP_LOGW(TAG, "read output failed: %s", esp_err_to_name(ret));
            }
        }
        else
        {
            ESP_LOGW(TAG, "fill input failed: %s", esp_err_to_name(ret));
        }

        (void)xQueueSend(s_free_queue, &slot, portMAX_DELAY);
        vTaskDelay(1);
    }
}

esp_err_t app_drone_ai_init(void)
{
    if (s_inited)
    {
        return ESP_OK;
    }

    for (uint32_t i = 0; i < DRONE_AI_SLOT_COUNT; i++) {
        s_slots[i].rgb = (uint8_t *)heap_caps_malloc(DRONE_AI_RGB_BYTES,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_slots[i].rgb == NULL)
        {
            ESP_LOGE(TAG, "slot alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }

    s_free_queue = xQueueCreate(DRONE_AI_SLOT_COUNT, sizeof(app_drone_ai_slot_t *));
    s_infer_queue = xQueueCreate(DRONE_AI_SLOT_COUNT, sizeof(app_drone_ai_slot_t *));
    if (s_free_queue == NULL || s_infer_queue == NULL)
    {
        ESP_LOGE(TAG, "queue create failed");
        return ESP_ERR_NO_MEM;
    }

    for (uint32_t i = 0; i < DRONE_AI_SLOT_COUNT; i++) {
        app_drone_ai_slot_t *slot = &s_slots[i];
        (void)xQueueSend(s_free_queue, &slot, 0);
    }

    taskENTER_CRITICAL(&s_ai_mux);
    s_inited = true;
    s_model_load_requested = false;
    s_model_ready = false;
    s_model_status = ESP_ERR_INVALID_STATE;
    s_confirmed = false;
    s_hit_count = 0;
    s_infer_count = 0;
    s_drop_count = 0;
    s_last_status_ms = 0;
    memset(&s_latest, 0, sizeof(s_latest));
    taskEXIT_CRITICAL(&s_ai_mux);

    /*
     * Keep the model loader on an internal stack. FbsLoader maps the flash
     * partition during construction, and that path is not reliable when the
     * calling task stack lives in PSRAM on ESP32-P4.
     */
    BaseType_t ok = xTaskCreatePinnedToCore(app_drone_ai_task,
                                            "drone_ai",
                                            DRONE_AI_TASK_STACK_SIZE,
                                            NULL,
                                            DRONE_AI_TASK_PRIORITY,
                                            &s_ai_task,
                                            DRONE_AI_TASK_CORE_ID);
    if (ok != pdPASS)
    {
        s_ai_task = NULL;
        taskENTER_CRITICAL(&s_ai_mux);
        s_inited = false;
        taskEXIT_CRITICAL(&s_ai_mux);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "drone ai init done (threshold=%.2f hits=%u)",
             (double)DRONE_AI_THRESHOLD,
             (unsigned)DRONE_AI_CONFIRM_HITS);
    return ESP_OK;
}

esp_err_t app_drone_ai_submit_frame(const uint8_t *rgb565,
                                    uint32_t width,
                                    uint32_t height,
                                    size_t len)
{
    if (!s_inited || s_free_queue == NULL || s_infer_queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (rgb565 == NULL || width == 0U || height == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len < (size_t)width * (size_t)height * 2U)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    taskENTER_CRITICAL(&s_ai_mux);
    const bool already_confirmed = s_confirmed;
    taskEXIT_CRITICAL(&s_ai_mux);
    if (already_confirmed)
    {
        return ESP_OK;
    }

    app_drone_ai_slot_t *slot = NULL;
    if (xQueueReceive(s_free_queue, &slot, 0) != pdTRUE || slot == NULL)
    {
        taskENTER_CRITICAL(&s_ai_mux);
        s_drop_count++;
        taskEXIT_CRITICAL(&s_ai_mux);
        return ESP_OK;
    }

    app_drone_ai_resize_rgb565_to_rgb888(rgb565, width, height, slot->rgb);
    slot->src_width = width;
    slot->src_height = height;
    slot->tick_ms = app_drone_ai_now_ms();

    taskENTER_CRITICAL(&s_ai_mux);
    s_submit_seq++;
    slot->seq = s_submit_seq;
    taskEXIT_CRITICAL(&s_ai_mux);

    if (xQueueSend(s_infer_queue, &slot, 0) != pdTRUE)
    {
        (void)xQueueSend(s_free_queue, &slot, 0);
        taskENTER_CRITICAL(&s_ai_mux);
        s_drop_count++;
        taskEXIT_CRITICAL(&s_ai_mux);
    }
    return ESP_OK;
}

bool app_drone_ai_get_latest_result(app_drone_ai_result_t *out)
{
    if (out == NULL)
    {
        return false;
    }

    taskENTER_CRITICAL(&s_ai_mux);
    *out = s_latest;
    taskEXIT_CRITICAL(&s_ai_mux);
    return out->valid;
}

bool app_drone_ai_is_model_ready(void)
{
    bool ready = false;
    taskENTER_CRITICAL(&s_ai_mux);
    ready = s_model_ready;
    taskEXIT_CRITICAL(&s_ai_mux);
    return ready;
}

static void app_drone_ai_request_model_load(void)
{
    bool should_notify = false;

    taskENTER_CRITICAL(&s_ai_mux);
    if (s_inited && !s_model_load_requested)
    {
        s_model_load_requested = true;
        should_notify = true;
    }
    taskEXIT_CRITICAL(&s_ai_mux);

    if (should_notify && s_ai_task != NULL)
    {
        xTaskNotifyGive(s_ai_task);
    }
}

esp_err_t app_drone_ai_wait_ready(uint32_t timeout_ms)
{
    const uint32_t start_ms = app_drone_ai_now_ms();

    app_drone_ai_request_model_load();

    while (1) {
        esp_err_t status = ESP_ERR_INVALID_STATE;
        bool ready = false;

        taskENTER_CRITICAL(&s_ai_mux);
        ready = s_model_ready;
        status = s_model_status;
        taskEXIT_CRITICAL(&s_ai_mux);

        if (ready)
        {
            return ESP_OK;
        }
        if (timeout_ms != UINT32_MAX && (app_drone_ai_now_ms() - start_ms) >= timeout_ms)
        {
            return (status == ESP_ERR_INVALID_STATE) ? ESP_ERR_TIMEOUT : status;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool app_drone_ai_is_drone_confirmed(void)
{
    bool confirmed = false;
    taskENTER_CRITICAL(&s_ai_mux);
    confirmed = s_confirmed;
    taskEXIT_CRITICAL(&s_ai_mux);
    return confirmed;
}

void app_drone_ai_reset_gate(void)
{
    taskENTER_CRITICAL(&s_ai_mux);
    s_confirmed = false;
    s_hit_count = 0;
    s_latest.confirmed = false;
    s_latest.hit_count = 0;
    taskEXIT_CRITICAL(&s_ai_mux);
}

void app_drone_ai_format_status(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0U)
    {
        return;
    }

    app_drone_ai_result_t latest = {};
    bool inited = false;

    taskENTER_CRITICAL(&s_ai_mux);
    latest = s_latest;
    inited = s_inited;
    taskEXIT_CRITICAL(&s_ai_mux);

    if (!inited)
    {
        snprintf(buf, buf_len, "ai: init");
        return;
    }
    if (!latest.valid)
    {
        snprintf(buf, buf_len, "ai: waiting");
        return;
    }

    const char *label = (latest.label == APP_DRONE_AI_CLASS_DRONE) ? "DRONE" : "NODRONE";
    const float score = (latest.label == APP_DRONE_AI_CLASS_DRONE) ? latest.drone_score : latest.nodrone_score;
    if (latest.confirmed)
    {
        snprintf(buf, buf_len, "ai: confirmed -> tag");
    }
    else
    {
        snprintf(buf,
                 buf_len,
                 "ai: %s %.2f %u/%u",
                 label,
                 (double)score,
                 (unsigned)latest.hit_count,
                 (unsigned)DRONE_AI_CONFIRM_HITS);
    }
}

void app_drone_ai_get_stats(app_drone_ai_stats_t *out)
{
    if (out == NULL)
    {
        return;
    }

    taskENTER_CRITICAL(&s_ai_mux);
    out->submitted = s_submit_seq;
    out->inferred = s_infer_count;
    out->dropped = s_drop_count;
    out->confirmed = s_confirmed;
    taskEXIT_CRITICAL(&s_ai_mux);
}
