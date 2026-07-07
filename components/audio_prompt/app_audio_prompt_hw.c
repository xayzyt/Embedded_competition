#include "app_audio_prompt_hw.h"

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "audio_prompt_hw";

#define APP_AUDIO_PROMPT_SAMPLE_RATE       22050U
#define APP_AUDIO_PROMPT_CHANNELS          1U
#define APP_AUDIO_PROMPT_BITS              16U
#define APP_AUDIO_PROMPT_VOLUME            65
#define APP_AUDIO_PROMPT_DMA_MIN_FREE      (48U * 1024U)
#define APP_AUDIO_PROMPT_DMA_MIN_LARGEST   (16U * 1024U)

typedef struct {
    i2s_chan_handle_t tx_channel;
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t codec_dev;
    bool codec_opened;
    bool ready;
} app_audio_prompt_hw_ctx_t;

static app_audio_prompt_hw_ctx_t s_hw = {0};

static size_t app_audio_prompt_dma_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

static size_t app_audio_prompt_dma_largest(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

bool app_audio_prompt_hw_dma_budget_ok(const char *stage)
{
    const size_t free_dma = app_audio_prompt_dma_free();
    const size_t largest_dma = app_audio_prompt_dma_largest();
    const bool ok = free_dma >= APP_AUDIO_PROMPT_DMA_MIN_FREE &&
                    largest_dma >= APP_AUDIO_PROMPT_DMA_MIN_LARGEST;
    if (!ok)
    {
        ESP_LOGW(TAG,
            "%s DMA budget low: free=%u largest=%u reserve=%u/%u",
            stage != NULL ? stage : "audio",
            (unsigned)free_dma,
            (unsigned)largest_dma,
            (unsigned)APP_AUDIO_PROMPT_DMA_MIN_FREE,
            (unsigned)APP_AUDIO_PROMPT_DMA_MIN_LARGEST);
    }
    return ok;
}

static void app_audio_prompt_hw_log_dma(const char *stage)
{
    ESP_LOGI(TAG,
        "%s: internal_dma_free=%u largest=%u minimum=%u",
        stage,
        (unsigned)app_audio_prompt_dma_free(),
        (unsigned)app_audio_prompt_dma_largest(),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}

void app_audio_prompt_hw_deinit(void)
{
    s_hw.ready = false;

    if (s_hw.codec_dev != NULL)
    {
        // open 失败也可能已部分启用 codec/data path，close 必须尽力执行。
        (void)esp_codec_dev_close(s_hw.codec_dev);
        s_hw.codec_opened = false;
        esp_codec_dev_delete(s_hw.codec_dev);
        s_hw.codec_dev = NULL;
    }
    if (s_hw.codec_if != NULL)
    {
        (void)audio_codec_delete_codec_if(s_hw.codec_if);
        s_hw.codec_if = NULL;
    }
    if (s_hw.ctrl_if != NULL)
    {
        (void)audio_codec_delete_ctrl_if(s_hw.ctrl_if);
        s_hw.ctrl_if = NULL;
    }
    if (s_hw.gpio_if != NULL)
    {
        (void)audio_codec_delete_gpio_if(s_hw.gpio_if);
        s_hw.gpio_if = NULL;
    }
    if (s_hw.data_if != NULL)
    {
        (void)audio_codec_delete_data_if(s_hw.data_if);
        s_hw.data_if = NULL;
    }
    if (s_hw.tx_channel != NULL)
    {
        // codec close 通常已经停用通道；重复 disable 的返回值可安全忽略。
        (void)i2s_channel_disable(s_hw.tx_channel);
        (void)i2s_del_channel(s_hw.tx_channel);
        s_hw.tx_channel = NULL;
    }
}

static esp_err_t app_audio_prompt_hw_create_i2s(void)
{
    i2s_chan_config_t channel_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    channel_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&channel_cfg, &s_hw.tx_channel, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "create TX-only I2S channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(APP_AUDIO_PROMPT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ret = i2s_channel_init_std_mode(s_hw.tx_channel, &std_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "initialize I2S TX mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2s_channel_enable(s_hw.tx_channel);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "enable I2S TX channel failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t app_audio_prompt_hw_create_codec(void)
{
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "shared BSP I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .tx_handle = s_hw.tx_channel,
        .rx_handle = NULL,
    };
    s_hw.data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_hw.data_if == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    if (i2c_cfg.bus_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_hw.ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    s_hw.gpio_if = audio_codec_new_gpio();
    if (s_hw.ctrl_if == NULL || s_hw.gpio_if == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    const esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = s_hw.ctrl_if,
        .gpio_if = s_hw.gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    s_hw.codec_if = es8311_codec_new(&codec_cfg);
    if (s_hw.codec_if == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_hw.codec_if,
        .data_if = s_hw.data_if,
    };
    s_hw.codec_dev = esp_codec_dev_new(&dev_cfg);
    if (s_hw.codec_dev == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    const int vol_ret = esp_codec_dev_set_out_vol(s_hw.codec_dev,
        APP_AUDIO_PROMPT_VOLUME);
    if (vol_ret != ESP_CODEC_DEV_OK)
    {
        ESP_LOGW(TAG, "set speaker volume failed: %d", vol_ret);
    }

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = APP_AUDIO_PROMPT_BITS,
        .channel = APP_AUDIO_PROMPT_CHANNELS,
        .channel_mask = 0,
        .sample_rate = APP_AUDIO_PROMPT_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    const int open_ret = esp_codec_dev_open(s_hw.codec_dev, &sample_info);
    if (open_ret != ESP_CODEC_DEV_OK)
    {
        ESP_LOGW(TAG, "open ES8311 speaker failed: %d", open_ret);
        return ESP_FAIL;
    }
    s_hw.codec_opened = true;
    return ESP_OK;
}

esp_err_t app_audio_prompt_hw_init(void)
{
    if (s_hw.ready)
    {
        return ESP_OK;
    }

    app_audio_prompt_hw_log_dma("before audio hardware init");
    if (!app_audio_prompt_hw_dma_budget_ok("before audio hardware init"))
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = app_audio_prompt_hw_create_i2s();
    if (ret == ESP_OK)
    {
        ret = app_audio_prompt_hw_create_codec();
    }
    if (ret != ESP_OK)
    {
        app_audio_prompt_hw_deinit();
        return ret;
    }

    if (!app_audio_prompt_hw_dma_budget_ok("after audio hardware init"))
    {
        app_audio_prompt_hw_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_hw.ready = true;
    app_audio_prompt_hw_log_dma("audio hardware ready");
    return ESP_OK;
}

bool app_audio_prompt_hw_is_ready(void)
{
    return s_hw.ready && s_hw.codec_opened && s_hw.codec_dev != NULL;
}

esp_err_t app_audio_prompt_hw_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_audio_prompt_hw_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > (size_t)INT32_MAX)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    const int ret = esp_codec_dev_write(s_hw.codec_dev, (void *)data, (int)len);
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
