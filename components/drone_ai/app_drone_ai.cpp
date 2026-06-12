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

static const char *TAG = "drone_ai";

#define DRONE_AI_MODEL_PARTITION_LABEL "model"
#define DRONE_AI_INPUT_W              128U
#define DRONE_AI_INPUT_H              128U
#define DRONE_AI_INPUT_CH             3U
#define DRONE_AI_INPUT_PIXELS         (DRONE_AI_INPUT_W * DRONE_AI_INPUT_H)
#define DRONE_AI_RGB_BYTES            (DRONE_AI_INPUT_PIXELS * DRONE_AI_INPUT_CH)
#define DRONE_AI_SLOT_COUNT           1U
#define DRONE_AI_TASK_STACK_SIZE      (24 * 1024)
// Keep inference off CPU1, where the full-frame camera display task runs.
#define DRONE_AI_TASK_PRIORITY        (tskIDLE_PRIORITY + 1)
#define DRONE_AI_TASK_CORE_ID         0
#define DRONE_AI_THRESHOLD            0.85f
#define DRONE_AI_CONFIRM_HITS         3U
#define DRONE_AI_HIT_MIN_INTERVAL_MS  800U
#define DRONE_AI_CONFIRM_DISPLAY_MS   1000U
#define DRONE_AI_MOTION_REJECT_DIFF   80U
#define DRONE_AI_MOTION_COOLDOWN_MS   500U
#define DRONE_AI_MALLOC_PSRAM_LIMIT   0U

typedef enum {
    APP_DRONE_AI_CLASS_NODRONE = 0,
    APP_DRONE_AI_CLASS_DRONE = 1,
} app_drone_ai_class_t;

typedef struct {
    bool valid;
    bool confirmed;
    app_drone_ai_class_t label;
    float nodrone_score;
    float drone_score;
    uint8_t hit_count;
    uint32_t motion_score;
    uint32_t frame_seq;
    uint32_t infer_ms;
} app_drone_ai_result_t;

typedef struct {
    uint8_t *rgb;
    uint32_t src_width;
    uint32_t src_height;
    uint32_t seq;
    uint32_t tick_ms;
    uint32_t motion_score;
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
static bool s_busy = false;
static bool s_confirmed = false;
static uint8_t s_hit_count = 0;
static uint32_t s_last_hit_ms = 0;
static uint32_t s_last_motion_reject_ms = 0;
static uint32_t s_submit_seq = 0;
static uint32_t s_infer_count = 0;
static uint32_t s_drop_count = 0;
static app_drone_ai_result_t s_latest = {};
static uint8_t *s_motion_prev_gray = nullptr;
static bool s_motion_prev_valid = false;

static dl::Model *s_model = nullptr;
static dl::TensorBase *s_model_input = nullptr;
static dl::TensorBase *s_model_output = nullptr;
static fbs::FbsLoader *s_fbs_loader = nullptr;
static fbs::FbsModel *s_fbs_model = nullptr;

static inline void app_drone_ai_clear_hit_window_locked(void)
{
    s_hit_count = 0;
    s_last_hit_ms = 0;
}

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

static bool app_drone_ai_input_is_nhwc(const std::vector<int> &shape)
{
    return shape.size() == 4U &&
           shape[1] == (int)DRONE_AI_INPUT_H &&
           shape[2] == (int)DRONE_AI_INPUT_W &&
           shape[3] == (int)DRONE_AI_INPUT_CH;
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
        s_model->dl::Model::~Model();
        heap_caps_free(s_model);
        s_model = nullptr;
    }
    if (s_fbs_model != nullptr)
    {
        delete s_fbs_model;
        s_fbs_model = nullptr;
    }
    if (s_fbs_loader != nullptr)
    {
        s_fbs_loader->fbs::FbsLoader::~FbsLoader();
        heap_caps_free(s_fbs_loader);
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

static inline uint8_t app_drone_ai_rgb888_to_gray(const uint8_t *rgb)
{
    return (uint8_t)(((uint32_t)rgb[0] * 77U +
                      (uint32_t)rgb[1] * 150U +
                      (uint32_t)rgb[2] * 29U) >> 8);
}

static uint32_t app_drone_ai_update_motion_score(const uint8_t *rgb)
{
    if (rgb == nullptr || s_motion_prev_gray == nullptr)
    {
        return 0;
    }
    uint32_t diff_sum = 0;
    uint32_t diff_count = 0;
    const bool prev_valid = s_motion_prev_valid;
    for (uint32_t i = 0; i < DRONE_AI_INPUT_PIXELS; i++) {
        const uint8_t gray = app_drone_ai_rgb888_to_gray(&rgb[(size_t)i * DRONE_AI_INPUT_CH]);
        if (prev_valid && ((i & 0x03U) == 0U))
        {
            const uint8_t prev = s_motion_prev_gray[i];
            diff_sum += (gray > prev) ? (gray - prev) : (prev - gray);
            diff_count++;
        }
        s_motion_prev_gray[i] = gray;
    }
    s_motion_prev_valid = true;
    return (prev_valid && diff_count > 0U) ? (diff_sum / diff_count) : 0U;
}

static esp_err_t app_drone_ai_load_model(void)
{
    if (s_model != nullptr)
    {
        return ESP_OK;
    }

    const app_drone_ai_malloc_psram_scope_t malloc_scope;
    void *loader_mem = heap_caps_malloc(sizeof(fbs::FbsLoader), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (loader_mem == nullptr)
    {
        ESP_LOGE(TAG, "fbs loader alloc failed");
        return ESP_ERR_NO_MEM;
    }
    s_fbs_loader = new (loader_mem) fbs::FbsLoader(DRONE_AI_MODEL_PARTITION_LABEL,
                                                   fbs::MODEL_LOCATION_IN_FLASH_PARTITION);

    s_fbs_model = s_fbs_loader->load(nullptr, false);
    if (s_fbs_model == nullptr)
    {
        ESP_LOGE(TAG, "fbs model load failed");
        app_drone_ai_release_model_objects();
        return ESP_FAIL;
    }

    void *model_mem = heap_caps_malloc(sizeof(dl::Model), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (model_mem == nullptr)
    {
        ESP_LOGE(TAG, "dl model alloc failed");
        app_drone_ai_release_model_objects();
        return ESP_ERR_NO_MEM;
    }
    s_model = new (model_mem) dl::Model(s_fbs_model, 0, dl::MEMORY_MANAGER_GREEDY);

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
                                       float drone_score,
                                       uint32_t motion_score,
                                       uint32_t tick_ms)
{
    app_drone_ai_result_t latest = {};

    latest.valid = true;
    latest.label = (drone_score >= nodrone_score) ? APP_DRONE_AI_CLASS_DRONE : APP_DRONE_AI_CLASS_NODRONE;
    latest.nodrone_score = nodrone_score;
    latest.drone_score = drone_score;
    latest.frame_seq = frame_seq;
    latest.infer_ms = infer_ms;
    latest.motion_score = motion_score;

    taskENTER_CRITICAL(&s_ai_mux);
    if (!s_confirmed)
    {
        const bool candidate_hit = (latest.label == APP_DRONE_AI_CLASS_DRONE) &&
                                   (drone_score >= DRONE_AI_THRESHOLD);
        // Fast camera motion can spike the classifier, so shaky frames do not count.
        const bool high_motion = motion_score >= DRONE_AI_MOTION_REJECT_DIFF;
        if (high_motion)
        {
            s_last_motion_reject_ms = tick_ms;
        }
        const bool motion_cooldown_done = (s_last_motion_reject_ms == 0U) ||
                                          ((tick_ms - s_last_motion_reject_ms) >= DRONE_AI_MOTION_COOLDOWN_MS);
        const bool stable_frame = !high_motion && motion_cooldown_done;
        if (candidate_hit && stable_frame)
        {
            // One inference frame can contribute at most one hit. Keep each
            // visible step on screen long enough for a live demonstration.
            if (s_last_hit_ms == 0U ||
                (tick_ms - s_last_hit_ms) >= DRONE_AI_HIT_MIN_INTERVAL_MS)
            {
                s_last_hit_ms = tick_ms;
                if (s_hit_count < DRONE_AI_CONFIRM_HITS)
                {
                    s_hit_count++;
                }
            }
        }
    }
    latest.hit_count = s_hit_count;
    latest.confirmed = s_confirmed;
    s_latest = latest;
    taskEXIT_CRITICAL(&s_ai_mux);
}

static void app_drone_ai_task(void *arg)
{
    (void)arg;

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    while (1) {
        esp_err_t load_ret = app_drone_ai_load_model();
        app_drone_ai_set_model_status(load_ret);
        if (load_ret == ESP_OK)
        {
            ESP_LOGI(TAG,
                     "model ready, task priority=%u core=%u slots=%u",
                     (unsigned)DRONE_AI_TASK_PRIORITY,
                     (unsigned)DRONE_AI_TASK_CORE_ID,
                     (unsigned)DRONE_AI_SLOT_COUNT);
            break;
        }
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
            taskENTER_CRITICAL(&s_ai_mux);
            s_busy = false;
            taskEXIT_CRITICAL(&s_ai_mux);
            float logits[2] = {0.0f, 0.0f};

            ret = app_drone_ai_read_logits(logits);
            if (ret == ESP_OK)
            {
                float nodrone_score = 0.0f;
                float drone_score = 0.0f;
                app_drone_ai_softmax2(logits, &nodrone_score, &drone_score);
                taskENTER_CRITICAL(&s_ai_mux);
                const uint8_t previous_hit_count = s_hit_count;
                taskEXIT_CRITICAL(&s_ai_mux);
                app_drone_ai_update_result(slot->seq,
                                           infer_ms,
                                           nodrone_score,
                                           drone_score,
                                           slot->motion_score,
                                           slot->tick_ms);

                taskENTER_CRITICAL(&s_ai_mux);
                s_infer_count++;
                const uint32_t infer_count = s_infer_count;
                const uint8_t hit_count = s_hit_count;
                const bool confirmed = s_confirmed;
                const bool confirmation_pending =
                    !s_confirmed && s_hit_count >= DRONE_AI_CONFIRM_HITS;
                taskEXIT_CRITICAL(&s_ai_mux);

                if (infer_count == 1U ||
                    hit_count != previous_hit_count ||
                    (infer_count % 20U) == 0U)
                {
                    ESP_LOGI(TAG,
                             "infer=%lu seq=%lu drone=%.3f hit=%u/%u motion=%lu time=%lums confirmed=%u",
                             (unsigned long)infer_count,
                             (unsigned long)slot->seq,
                             (double)drone_score,
                             (unsigned)hit_count,
                             (unsigned)DRONE_AI_CONFIRM_HITS,
                             (unsigned long)slot->motion_score,
                             (unsigned long)infer_ms,
                             confirmed ? 1U : 0U);
                }

                if (confirmation_pending)
                {
                    // Leave 3/3 visible before switching the camera route to
                    // AprilTag. A reset during this delay cancels confirmation.
                    vTaskDelay(pdMS_TO_TICKS(DRONE_AI_CONFIRM_DISPLAY_MS));
                    taskENTER_CRITICAL(&s_ai_mux);
                    if (s_hit_count >= DRONE_AI_CONFIRM_HITS)
                    {
                        s_confirmed = true;
                        s_latest.confirmed = true;
                    }
                    taskEXIT_CRITICAL(&s_ai_mux);
                }
            }
        }

        taskENTER_CRITICAL(&s_ai_mux);
        s_busy = false;
        taskEXIT_CRITICAL(&s_ai_mux);
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

    s_motion_prev_gray = (uint8_t *)heap_caps_malloc(DRONE_AI_INPUT_PIXELS,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_motion_prev_gray == NULL)
    {
        ESP_LOGE(TAG, "motion buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }
    memset(s_motion_prev_gray, 0, DRONE_AI_INPUT_PIXELS);

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
    s_busy = false;
    s_confirmed = false;
    app_drone_ai_clear_hit_window_locked();
    s_last_motion_reject_ms = 0;
    s_infer_count = 0;
    s_drop_count = 0;
    s_motion_prev_valid = false;
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
    const bool gate_complete_or_pending =
        s_confirmed || s_hit_count >= DRONE_AI_CONFIRM_HITS;
    taskEXIT_CRITICAL(&s_ai_mux);
    if (gate_complete_or_pending)
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

    taskENTER_CRITICAL(&s_ai_mux);
    s_busy = true;
    taskEXIT_CRITICAL(&s_ai_mux);

    app_drone_ai_resize_rgb565_to_rgb888(rgb565, width, height, slot->rgb);
    slot->src_width = width;
    slot->src_height = height;
    slot->motion_score = app_drone_ai_update_motion_score(slot->rgb);
    slot->tick_ms = app_drone_ai_now_ms();
    if (slot->motion_score >= DRONE_AI_MOTION_REJECT_DIFF)
    {
        taskENTER_CRITICAL(&s_ai_mux);
        s_last_motion_reject_ms = slot->tick_ms;
        s_latest.motion_score = slot->motion_score;
        taskEXIT_CRITICAL(&s_ai_mux);
    }

    taskENTER_CRITICAL(&s_ai_mux);
    s_submit_seq++;
    slot->seq = s_submit_seq;
    taskEXIT_CRITICAL(&s_ai_mux);
    if (slot->seq == 1U)
    {
        ESP_LOGI(TAG,
                 "first frame submitted: %lux%lu",
                 (unsigned long)width,
                 (unsigned long)height);
    }

    if (xQueueSend(s_infer_queue, &slot, 0) != pdTRUE)
    {
        taskENTER_CRITICAL(&s_ai_mux);
        s_busy = false;
        s_drop_count++;
        taskEXIT_CRITICAL(&s_ai_mux);
        (void)xQueueSend(s_free_queue, &slot, 0);
    }
    return ESP_OK;
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

bool app_drone_ai_is_busy(void)
{
    bool busy = false;
    taskENTER_CRITICAL(&s_ai_mux);
    busy = s_busy;
    taskEXIT_CRITICAL(&s_ai_mux);
    return busy;
}

void app_drone_ai_reset_gate(void)
{
    taskENTER_CRITICAL(&s_ai_mux);
    s_confirmed = false;
    app_drone_ai_clear_hit_window_locked();
    s_last_motion_reject_ms = 0;
    s_motion_prev_valid = false;
    s_latest.confirmed = false;
    s_latest.hit_count = 0;
    s_latest.motion_score = 0;
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
                 "ai: %s %.2f %u/%u m%lu",
                 label,
                 (double)score,
                 (unsigned)latest.hit_count,
                 (unsigned)DRONE_AI_CONFIRM_HITS,
                 (unsigned long)latest.motion_score);
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
    out->hit_count = s_hit_count;
    out->confirm_hits = DRONE_AI_CONFIRM_HITS;
    out->confirmed = s_confirmed;
    taskEXIT_CRITICAL(&s_ai_mux);
}
