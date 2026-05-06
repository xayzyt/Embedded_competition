/*
 * app_ch32_link.c - ESP32-P4 与 CH32 串口通信 (精简版)
 *
 * 已砍掉: VER 版本号、老协议 ASCII、EVENT/ERROR/HEARTBEAT 独立类型
 * 帧格式: SOF1 SOF2 TYPE CMD SEQ LEN [PAYLOAD] CRC16
 */

#include "app_ch32_link.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_ch32_link";

/* -------------------- 内部常量 -------------------- */
#define CH32_EVT_READY           BIT0
#define CH32_EVT_ACK             BIT1
#define CH32_EVT_NACK            BIT2

#ifndef APP_CH32_LINK_READY_STALE_MS
#define APP_CH32_LINK_READY_STALE_MS (3000U)
#endif

/* -------------------- 运行上下文 -------------------- */
typedef struct {
    uart_port_t uart_num;
    EventGroupHandle_t event_group;
    TaskHandle_t rx_task;
    app_ch32_line_cb_t cb;
    void *user_ctx;
    bool inited;
    bool ready;
    int32_t last_weight_g;
    bool has_weight;
    app_ch32_proto_cmd_t last_ack_cmd;
    uint8_t last_ack_seq;
    app_ch32_proto_cmd_t last_nack_cmd;
    uint8_t last_nack_seq;
    uint8_t last_nack_error;
    uint8_t next_seq;
    TickType_t last_rx_tick;
} app_ch32_link_ctx_t;

static app_ch32_link_ctx_t s_ctx = {0};

/* -------------------- CRC16 -------------------- */
static uint16_t app_ch32_crc16_ibm(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U)
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* -------------------- 字节序辅助 -------------------- */
static int32_t app_ch32_read_i32_le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24));
}

static uint16_t app_ch32_read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* 兼容字符→命令码映射 (app_ctrl 使用) */
static app_ch32_proto_cmd_t app_ch32_char_cmd_to_proto(char cmd)
{
    switch (cmd) {
    case 'P': return APP_CH32_PROTO_CMD_PROBE_READY;
    case 'A': return APP_CH32_PROTO_CMD_START_DOCK;
    case 'I': return APP_CH32_PROTO_CMD_QUERY_STATUS;
    case 'K': return APP_CH32_PROTO_CMD_RESET_FAULT;
    case 'W': return APP_CH32_PROTO_CMD_READ_WEIGHT;
    case 'S': return APP_CH32_PROTO_CMD_ABORT;
    default:  return APP_CH32_PROTO_CMD_NONE;
    }
}

static void app_ch32_reset_ack_state(void)
{
    s_ctx.last_ack_cmd = APP_CH32_PROTO_CMD_NONE;
    s_ctx.last_ack_seq = 0;
    s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
    s_ctx.last_nack_seq = 0;
    s_ctx.last_nack_error = APP_CH32_ERR_NONE;
}

/* -------------------- 名称辅助 -------------------- */

const char *app_ch32_link_proto_stage_name(app_ch32_proto_stage_t stage)
{
    switch (stage) {
    case APP_CH32_STAGE_IDLE:            return "IDLE";
    case APP_CH32_STAGE_READY:           return "READY";
    case APP_CH32_STAGE_DOOR_OPENING:    return "DOOR_OPENING";
    case APP_CH32_STAGE_DOOR_OPENED:     return "DOOR_OPENED";
    case APP_CH32_STAGE_TRAY_EXTENDING:  return "TRAY_EXTENDING";
    case APP_CH32_STAGE_TRAY_EXTENDED:   return "TRAY_EXTENDED";
    case APP_CH32_STAGE_WAITING_CARGO:   return "WAITING_CARGO";
    case APP_CH32_STAGE_CARGO_DETECTED:  return "CARGO_DETECTED";
    case APP_CH32_STAGE_TRAY_RETRACTING: return "TRAY_RETRACTING";
    case APP_CH32_STAGE_TRAY_RETRACTED:  return "TRAY_RETRACTED";
    case APP_CH32_STAGE_DOOR_CLOSING:    return "DOOR_CLOSING";
    case APP_CH32_STAGE_SAFE_LOCKED:     return "SAFE_LOCKED";
    case APP_CH32_STAGE_COMPLETE:        return "COMPLETE";
    case APP_CH32_STAGE_FAULT:           return "FAULT";
    case APP_CH32_STAGE_UNKNOWN:
    default:                             return "UNKNOWN";
    }
}

const char *app_ch32_link_proto_error_name(uint8_t err)
{
    switch ((app_ch32_proto_error_t)err) {
    case APP_CH32_ERR_NONE:        return "NONE";
    case APP_CH32_ERR_TIMEOUT:     return "TIMEOUT";
    case APP_CH32_ERR_LIMIT:       return "LIMIT";
    case APP_CH32_ERR_WEIGHT:      return "WEIGHT";
    case APP_CH32_ERR_JAM:         return "JAM";
    case APP_CH32_ERR_SENSOR:      return "SENSOR";
    case APP_CH32_ERR_SAFETY:      return "SAFETY";
    case APP_CH32_ERR_BUSY:        return "BUSY";
    case APP_CH32_ERR_UNKNOWN_CMD: return "UNKNOWN_CMD";
    case APP_CH32_ERR_BAD_CRC:     return "BAD_CRC";
    case APP_CH32_ERR_INTERNAL:    return "INTERNAL";
    default:                       return "UNKNOWN";
    }
}

static bool app_ch32_proto_stage_indicates_ready(app_ch32_proto_stage_t stage, uint16_t flags)
{
    if ((flags & APP_CH32_FLAG_READY) != 0U) return true;
    switch (stage) {
    case APP_CH32_STAGE_IDLE:
    case APP_CH32_STAGE_READY:
    case APP_CH32_STAGE_COMPLETE:
    case APP_CH32_STAGE_SAFE_LOCKED:
        return true;
    default:
        return false;
    }
}

/* -------------------- Ready 过期判断 -------------------- */
static bool app_ch32_ready_is_fresh(void)
{
    if (!s_ctx.ready) return false;
    const TickType_t age_ticks = xTaskGetTickCount() - s_ctx.last_rx_tick;
    if (age_ticks > pdMS_TO_TICKS(APP_CH32_LINK_READY_STALE_MS))
    {
        s_ctx.ready = false;
        if (s_ctx.event_group != NULL)
            xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);
        return false;
    }
    return true;
}

/* -------------------- 接收副作用 + 分发 -------------------- */
static void app_ch32_apply_common_side_effects(const app_ch32_line_t *msg)
{
    if (msg == NULL) return;
    s_ctx.last_rx_tick = xTaskGetTickCount();

    /* STATUS 类型统一承载: 状态/事件/错误/心跳 */
    if (msg->type == APP_CH32_LINE_PROTO_STATUS && msg->payload_len >= 8U)
    {
        s_ctx.last_weight_g = msg->proto_weight_g;
        s_ctx.has_weight = true;
        s_ctx.ready = app_ch32_proto_stage_indicates_ready(msg->proto_stage, msg->proto_flags);
        if (s_ctx.ready)
            xEventGroupSetBits(s_ctx.event_group, CH32_EVT_READY);
        else
            xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);
    }

    if (msg->type == APP_CH32_LINE_PROTO_ACK)
    {
        s_ctx.last_ack_cmd = (app_ch32_proto_cmd_t)msg->proto_cmd;
        s_ctx.last_ack_seq = msg->proto_seq;
        xEventGroupSetBits(s_ctx.event_group, CH32_EVT_ACK);
    }

    if (msg->type == APP_CH32_LINE_PROTO_NACK)
    {
        s_ctx.last_nack_cmd = (app_ch32_proto_cmd_t)msg->proto_cmd;
        s_ctx.last_nack_seq = msg->proto_seq;
        s_ctx.last_nack_error = msg->proto_detail;
        xEventGroupSetBits(s_ctx.event_group, CH32_EVT_NACK);
    }
}

static void app_ch32_dispatch_msg(const app_ch32_line_t *msg)
{
    if (msg == NULL) return;
    app_ch32_apply_common_side_effects(msg);
    if (s_ctx.cb != NULL)
        s_ctx.cb(msg, s_ctx.user_ctx);
    ESP_LOGD(TAG, "CH32 rx: %s", msg->line);
}

/* -------------------- 协议帧解析 -------------------- */

/*
 * 解析一帧二进制协议帧。帧格式 :
 *   [0]=SOF1 [1]=SOF2 [2]=TYPE [3]=CMD [4]=SEQ [5]=LEN
 *   [6..5+LEN]=PAYLOAD [6+LEN]=CRC_L [7+LEN]=CRC_H
 */
static bool app_ch32_parse_proto_frame(const uint8_t *frame, size_t frame_len, app_ch32_line_t *out)
{
    if (frame == NULL || out == NULL) return false;
    if (frame_len < APP_CH32_PROTO_MIN_FRAME) return false;
    if (frame[0] != APP_CH32_PROTO_SOF1 || frame[1] != APP_CH32_PROTO_SOF2) return false;

    const uint8_t payload_len = frame[5];
    if ((size_t)(APP_CH32_PROTO_OVERHEAD + payload_len) != frame_len) return false;

    /* CRC 校验: 覆盖 SOF1 到 PAYLOAD 末尾 (6+payload_len 字节) */
    const uint16_t crc_expect = app_ch32_read_u16_le(&frame[6 + payload_len]);
    const uint16_t crc_actual = app_ch32_crc16_ibm(frame, (size_t)(6U + payload_len));
    if (crc_expect != crc_actual)
    {
        ESP_LOGD(TAG, "proto crc mismatch, expect=0x%04x actual=0x%04x", crc_expect, crc_actual);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->proto_type = frame[2];
    out->proto_cmd  = frame[3];
    out->proto_seq  = frame[4];
    out->payload_len = payload_len;
    if (payload_len > 0U)
        memcpy(out->payload, &frame[6], payload_len);

    switch ((app_ch32_proto_type_t)out->proto_type) {
    case APP_CH32_PROTO_TYPE_ACK:
        out->type = APP_CH32_LINE_PROTO_ACK;
        snprintf(out->line, sizeof(out->line),
            "ACK cmd=0x%02X seq=%u", out->proto_cmd, (unsigned)out->proto_seq);
        break;

    case APP_CH32_PROTO_TYPE_NACK:
        out->type = APP_CH32_LINE_PROTO_NACK;
        out->proto_detail = (payload_len >= 1U) ? out->payload[0] : APP_CH32_ERR_INTERNAL;
        snprintf(out->line, sizeof(out->line),
            "NACK cmd=0x%02X seq=%u err=%s",
            out->proto_cmd, (unsigned)out->proto_seq,
            app_ch32_link_proto_error_name(out->proto_detail));
        break;

    case APP_CH32_PROTO_TYPE_STATUS:
        /* STATUS 统一承载: 状态/事件/错误/心跳 */
        out->type = APP_CH32_LINE_PROTO_STATUS;
        if (payload_len >= 8U)
        {
            out->proto_stage   = (app_ch32_proto_stage_t)out->payload[0];
            out->proto_detail  = out->payload[1];  /* 0=正常, 非0=错误码 */
            out->proto_flags   = app_ch32_read_u16_le(&out->payload[2]);
            out->proto_weight_g = app_ch32_read_i32_le(&out->payload[4]);
        }
        if (out->proto_detail != APP_CH32_ERR_NONE)
        {
            snprintf(out->line, sizeof(out->line),
                "STS ERR stage=%s err=%s flags=0x%04X w=%ldg",
                app_ch32_link_proto_stage_name(out->proto_stage),
                app_ch32_link_proto_error_name(out->proto_detail),
                (unsigned)out->proto_flags, (long)out->proto_weight_g);
        }
        else
        {
            snprintf(out->line, sizeof(out->line),
                "STS stage=%s flags=0x%04X w=%ldg",
                app_ch32_link_proto_stage_name(out->proto_stage),
                (unsigned)out->proto_flags, (long)out->proto_weight_g);
        }
        break;

    default:
        out->type = APP_CH32_LINE_UNKNOWN;
        break;
    }
    return true;
}

/* -------------------- UART 接收任务 -------------------- */
static void app_ch32_link_rx_task(void *arg)
{
    uint8_t ch = 0;
    uint8_t proto_buf[APP_CH32_PROTO_OVERHEAD + APP_CH32_LINK_PROTO_MAX_PAYLOAD];
    size_t proto_len = 0;
    size_t proto_expect = 0;
    (void)arg;

    while (1) {
        int len = uart_read_bytes(s_ctx.uart_num, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        if (proto_len == 0U)
        {
            if (ch != APP_CH32_PROTO_SOF1) continue;
            proto_buf[proto_len++] = ch;
            proto_expect = 0;
            continue;
        }
        if (proto_len == 1U)
        {
            if (ch == APP_CH32_PROTO_SOF2)
            {
                proto_buf[proto_len++] = ch;
                continue;
            }
            proto_len = (ch == APP_CH32_PROTO_SOF1) ? 1U : 0U;
            proto_buf[0] = (proto_len == 1U) ? ch : 0U;
            proto_expect = 0;
            continue;
        }
        if (proto_len < sizeof(proto_buf))
            proto_buf[proto_len++] = ch;
        else
        {
            proto_len = 0;
            proto_expect = 0;
            continue;
        }

        /* 收到第 6 字节 (LEN) 后计算期望帧长 */
        if (proto_len == 6U)
        {
            const uint8_t payload_len = proto_buf[5];
            if (payload_len > APP_CH32_LINK_PROTO_MAX_PAYLOAD)
            {
                proto_len = 0;
                proto_expect = 0;
                ESP_LOGW(TAG, "proto payload too large: %u", (unsigned)payload_len);
                continue;
            }
            proto_expect = APP_CH32_PROTO_OVERHEAD + payload_len;
        }

        if (proto_expect >= APP_CH32_PROTO_MIN_FRAME && proto_len == proto_expect)
        {
            app_ch32_line_t msg = {0};
            if (app_ch32_parse_proto_frame(proto_buf, proto_len, &msg))
                app_ch32_dispatch_msg(&msg);
            proto_len = 0;
            proto_expect = 0;
        }
    }
}

/* -------------------- UART 发送路径 -------------------- */
static esp_err_t app_ch32_link_prepare_tx_idle(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << APP_CH32_LINK_TX_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config tx idle failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_CH32_LINK_TX_GPIO, 1), TAG, "gpio_set_level tx idle failed");
    return ESP_OK;
}

/*
 * 组装并发送一帧二进制协议命令。
 * 帧格式: SOF1 SOF2 TYPE_CMD cmd seq len [payload] CRC16
 * CRC 覆盖前 6+payload_len 字节
 */
esp_err_t app_ch32_link_send_proto(app_ch32_proto_cmd_t cmd,
    const void *payload, uint8_t payload_len, uint8_t *out_seq)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(payload_len <= APP_CH32_LINK_PROTO_MAX_PAYLOAD,
        ESP_ERR_INVALID_ARG, TAG, "payload too large");
    ESP_RETURN_ON_FALSE((payload_len == 0U) || (payload != NULL),
        ESP_ERR_INVALID_ARG, TAG, "payload is null");

    uint8_t frame[APP_CH32_PROTO_OVERHEAD + APP_CH32_LINK_PROTO_MAX_PAYLOAD];
    const uint8_t seq = ++s_ctx.next_seq;
    size_t idx = 0;

    frame[idx++] = APP_CH32_PROTO_SOF1;
    frame[idx++] = APP_CH32_PROTO_SOF2;
    frame[idx++] = (uint8_t)APP_CH32_PROTO_TYPE_CMD;
    frame[idx++] = (uint8_t)cmd;
    frame[idx++] = seq;
    frame[idx++] = payload_len;

    if (payload_len > 0U && payload != NULL)
    {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }

    /* CRC 覆盖 SOF1 到 PAYLOAD 末尾 = 6+payload_len 字节 */
    const uint16_t crc = app_ch32_crc16_ibm(frame, idx);
    frame[idx++] = (uint8_t)(crc & 0xFFU);
    frame[idx++] = (uint8_t)((crc >> 8) & 0xFFU);

    int written = uart_write_bytes(s_ctx.uart_num, (const char *)frame, idx);
    ESP_RETURN_ON_FALSE(written == (int)idx, ESP_FAIL, TAG, "uart_write_bytes proto failed");
    if (out_seq != NULL) *out_seq = seq;

    ESP_LOGD(TAG, "ESP32 => CH32 proto: cmd=0x%02X seq=%u len=%u",
        (unsigned)cmd, (unsigned)seq, (unsigned)payload_len);
    return ESP_OK;
}

/* -------------------- ACK 等待 -------------------- */
static esp_err_t app_ch32_link_wait_ack_for_cmd(app_ch32_proto_cmd_t cmd, uint8_t seq, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    uint32_t slice_ms = APP_CH32_LINK_ACK_POLL_MS;
    if (slice_ms == 0U) slice_ms = 50U;

    while (elapsed_ms < timeout_ms) {
        uint32_t this_wait = slice_ms;
        if ((elapsed_ms + this_wait) > timeout_ms)
            this_wait = timeout_ms - elapsed_ms;

        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
            CH32_EVT_ACK | CH32_EVT_NACK, pdTRUE, pdFALSE, pdMS_TO_TICKS(this_wait));
        elapsed_ms += this_wait;

        if ((bits & (CH32_EVT_ACK | CH32_EVT_NACK)) == 0U) continue;

        if ((bits & CH32_EVT_ACK) != 0U && s_ctx.last_ack_cmd == cmd && s_ctx.last_ack_seq == seq)
            return ESP_OK;

        if ((bits & CH32_EVT_NACK) != 0U && s_ctx.last_nack_cmd == cmd && s_ctx.last_nack_seq == seq)
        {
            ESP_LOGW(TAG, "CH32 rejected cmd=0x%02X seq=%u err=%s",
                (unsigned)cmd, (unsigned)seq,
                app_ch32_link_proto_error_name(s_ctx.last_nack_error));
            s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
            return ESP_ERR_INVALID_RESPONSE;
        }

        /* 不匹配的旧事件，丢弃继续等 */
        if ((bits & CH32_EVT_ACK) != 0U) s_ctx.last_ack_cmd = APP_CH32_PROTO_CMD_NONE;
        if ((bits & CH32_EVT_NACK) != 0U) s_ctx.last_nack_cmd = APP_CH32_PROTO_CMD_NONE;
    }
    return ESP_ERR_TIMEOUT;
}

/* -------------------- 公开接口 -------------------- */

esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(!s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    s_ctx.uart_num = APP_CH32_LINK_UART_PORT;
    s_ctx.cb = cb;
    s_ctx.user_ctx = user_ctx;
    s_ctx.ready = false;
    s_ctx.has_weight = false;
    app_ch32_reset_ack_state();
    s_ctx.next_seq = 0;
    s_ctx.last_rx_tick = 0;

    uart_config_t uart_cfg = {
        .baud_rate = APP_CH32_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(app_ch32_link_prepare_tx_idle(), TAG, "prepare tx idle failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(s_ctx.uart_num, APP_CH32_LINK_RX_BUF_SIZE, 0, 0, NULL, 0),
        TAG, "uart_driver_install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(s_ctx.uart_num, &uart_cfg), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(s_ctx.uart_num,
        APP_CH32_LINK_TX_GPIO, APP_CH32_LINK_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        TAG, "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(APP_CH32_LINK_RX_GPIO, GPIO_PULLUP_ONLY),
        TAG, "set rx pull-up failed");

    s_ctx.event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_ctx.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group create failed");

    BaseType_t ok = xTaskCreate(app_ch32_link_rx_task, "ch32_rx", 4096, NULL, 8, &s_ctx.rx_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task create failed");

    s_ctx.inited = true;
    ESP_LOGI(TAG, "uart%d init ok, tx=%d rx=%d baud=%d",
        s_ctx.uart_num, APP_CH32_LINK_TX_GPIO, APP_CH32_LINK_RX_GPIO, APP_CH32_LINK_BAUD_RATE);
    return ESP_OK;
}

esp_err_t app_ch32_link_send_cmd_and_wait_ack(char cmd, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    const app_ch32_proto_cmd_t proto_cmd = app_ch32_char_cmd_to_proto(cmd);
    ESP_RETURN_ON_FALSE(proto_cmd != APP_CH32_PROTO_CMD_NONE,
        ESP_ERR_INVALID_ARG, TAG, "unknown cmd: %c", cmd);

    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK | CH32_EVT_NACK);
    app_ch32_reset_ack_state();

    uint8_t seq = 0;
    ESP_RETURN_ON_ERROR(app_ch32_link_send_proto(proto_cmd, NULL, 0, &seq), TAG, "proto send failed");
    return app_ch32_link_wait_ack_for_cmd(proto_cmd, seq, timeout_ms);
}

esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (app_ch32_ready_is_fresh()) return ESP_OK;

    s_ctx.ready = false;
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);

    ESP_RETURN_ON_ERROR(app_ch32_link_send_proto(APP_CH32_PROTO_CMD_PROBE_READY, NULL, 0, NULL),
        TAG, "proto probe send failed");

    EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group, CH32_EVT_READY,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return ((bits & CH32_EVT_READY) != 0U) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool app_ch32_link_is_ready(void)
{
    return app_ch32_ready_is_fresh();
}

bool app_ch32_link_last_weight(int32_t *out_weight_g)
{
    if (!s_ctx.has_weight || out_weight_g == NULL) return false;
    *out_weight_g = s_ctx.last_weight_g;
    return true;
}
