#include "app_ch32_link.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
static const char *TAG = "app_ch32_link";
#define CH32_EVT_READY           BIT0
#define CH32_EVT_ACK             BIT1
#define APP_CH32_PROTO_OVERHEAD  (9U)
#define APP_CH32_PROTO_MIN_FRAME (APP_CH32_PROTO_OVERHEAD)
#define APP_CH32_PROTO_MAX_FRAME (APP_CH32_PROTO_OVERHEAD + APP_CH32_LINK_PROTO_MAX_PAYLOAD)
typedef struct {
    uart_port_t uart_num;
    EventGroupHandle_t event_group;
    TaskHandle_t rx_task;
    app_ch32_line_cb_t cb;
    void *user_ctx;
    bool inited;
    bool ready;
    bool proto_online;
    int32_t last_weight_g;
    bool has_weight;
    char last_ack_cmd;
    uint8_t next_seq;
} app_ch32_link_ctx_t;
static app_ch32_link_ctx_t s_ctx = {0};
static uint16_t app_ch32_crc16_ibm(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
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
static app_ch32_proto_cmd_t app_ch32_legacy_cmd_to_proto(char cmd)
{
    switch (cmd) {
        case 'P': return APP_CH32_PROTO_CMD_PROBE_READY;
        case 'A': return APP_CH32_PROTO_CMD_START_DOCK;
        case 'O': return APP_CH32_PROTO_CMD_OPEN_DOOR;
        case 'C': return APP_CH32_PROTO_CMD_CLOSE_DOOR;
        case 'E': return APP_CH32_PROTO_CMD_EXTEND_TRAY;
        case 'R': return APP_CH32_PROTO_CMD_RETRACT_TRAY;
        case 'I': return APP_CH32_PROTO_CMD_QUERY_STATUS;
        case 'K': return APP_CH32_PROTO_CMD_RESET_FAULT;
        case 'W': return APP_CH32_PROTO_CMD_READ_WEIGHT;
        case 'S': return APP_CH32_PROTO_CMD_ABORT;
        default:  return APP_CH32_PROTO_CMD_NONE;
    }
}
static char app_ch32_proto_cmd_to_legacy(app_ch32_proto_cmd_t cmd)
{
    switch (cmd) {
        case APP_CH32_PROTO_CMD_PROBE_READY:  return 'P';
        case APP_CH32_PROTO_CMD_START_DOCK:   return 'A';
        case APP_CH32_PROTO_CMD_OPEN_DOOR:    return 'O';
        case APP_CH32_PROTO_CMD_CLOSE_DOOR:   return 'C';
        case APP_CH32_PROTO_CMD_EXTEND_TRAY:  return 'E';
        case APP_CH32_PROTO_CMD_RETRACT_TRAY: return 'R';
        case APP_CH32_PROTO_CMD_QUERY_STATUS: return 'I';
        case APP_CH32_PROTO_CMD_RESET_FAULT:  return 'K';
        case APP_CH32_PROTO_CMD_READ_WEIGHT:  return 'W';
        case APP_CH32_PROTO_CMD_ABORT:        return 'S';
        default:                              return 0;
    }
}
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
static app_ch32_line_type_t app_ch32_classify_legacy_line(const char *line)
{
    if (strncmp(line, "[ACK]", 5) == 0) {
        return APP_CH32_LINE_ACK;
    }
    if (strncmp(line, "[STS]", 5) == 0) {
        return APP_CH32_LINE_STATUS;
    }
    if (strncmp(line, "[ERR]", 5) == 0) {
        return APP_CH32_LINE_ERROR;
    }
    if (strncmp(line, "[DBG]", 5) == 0) {
        return APP_CH32_LINE_DEBUG;
    }
    return APP_CH32_LINE_UNKNOWN;
}
static bool app_ch32_proto_stage_indicates_ready(app_ch32_proto_stage_t stage, uint16_t flags)
{
    if ((flags & APP_CH32_FLAG_READY) != 0U) {
        return true;
    }
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
static void app_ch32_apply_common_side_effects(const app_ch32_line_t *msg)
{
    if (msg == NULL) {
        return;
    }
    if (msg->is_proto) {
        s_ctx.proto_online = true;
        if ((msg->type == APP_CH32_LINE_PROTO_STATUS) ||
            (msg->type == APP_CH32_LINE_PROTO_EVENT) ||
            (msg->type == APP_CH32_LINE_PROTO_ERROR) ||
            (msg->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
            if (msg->payload_len >= 8U) {
                s_ctx.last_weight_g = msg->proto_weight_g;
                s_ctx.has_weight = true;
            }
            if (app_ch32_proto_stage_indicates_ready(msg->proto_stage, msg->proto_flags)) {
                s_ctx.ready = true;
                xEventGroupSetBits(s_ctx.event_group, CH32_EVT_READY);
            }
        }
        if (msg->type == APP_CH32_LINE_PROTO_ACK) {
            s_ctx.last_ack_cmd = app_ch32_proto_cmd_to_legacy((app_ch32_proto_cmd_t)msg->proto_cmd);
            xEventGroupSetBits(s_ctx.event_group, CH32_EVT_ACK);
        }
        return;
    }
    if (msg->type == APP_CH32_LINE_STATUS) {
        if (strstr(msg->line, "CH32_READY") != NULL) {
            s_ctx.ready = true;
            xEventGroupSetBits(s_ctx.event_group, CH32_EVT_READY);
        }
        const char *w = strstr(msg->line, "WEIGHT=");
        if (w != NULL) {
            s_ctx.last_weight_g = (int32_t)strtol(w + 7, NULL, 10);
            s_ctx.has_weight = true;
        }
    } else if (msg->type == APP_CH32_LINE_ACK) {
        const char *p = msg->line + 5;
        while (*p == ' ') {
            p++;
        }
        s_ctx.last_ack_cmd = *p;
        xEventGroupSetBits(s_ctx.event_group, CH32_EVT_ACK);
    }
}
static void app_ch32_dispatch_msg(const app_ch32_line_t *msg)
{
    if (msg == NULL) {
        return;
    }
    app_ch32_apply_common_side_effects(msg);
    if (s_ctx.cb != NULL) {
        s_ctx.cb(msg, s_ctx.user_ctx);
    }
    if (msg->is_proto) {
        ESP_LOGI(TAG, "CH32 <=proto=> %s", msg->line);
    } else {
        ESP_LOGI(TAG, "CH32 => %s", msg->line);
    }
}
static void app_ch32_dispatch_legacy_line(const char *line)
{
    app_ch32_line_t msg = {0};
    msg.type = app_ch32_classify_legacy_line(line);
    msg.is_proto = false;
    strlcpy(msg.line, line, sizeof(msg.line));
    app_ch32_dispatch_msg(&msg);
}
static bool app_ch32_parse_proto_frame(const uint8_t *frame, size_t frame_len, app_ch32_line_t *out)
{
    if ((frame == NULL) || (out == NULL)) {
        return false;
    }
    if (frame_len < APP_CH32_PROTO_MIN_FRAME) {
        return false;
    }
    if ((frame[0] != APP_CH32_PROTO_SOF1) || (frame[1] != APP_CH32_PROTO_SOF2)) {
        return false;
    }
    if (frame[2] != APP_CH32_PROTO_VER) {
        return false;
    }
    const uint8_t payload_len = frame[6];
    if ((size_t)(APP_CH32_PROTO_OVERHEAD + payload_len) != frame_len) {
        return false;
    }
    const uint16_t crc_expect = app_ch32_read_u16_le(&frame[7 + payload_len]);
    const uint16_t crc_actual = app_ch32_crc16_ibm(&frame[2], (size_t)(5U + payload_len));
    if (crc_expect != crc_actual) {
        ESP_LOGW(TAG, "proto crc mismatch, expect=0x%04x actual=0x%04x", crc_expect, crc_actual);
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->is_proto = true;
    out->proto_type = frame[3];
    out->proto_cmd = frame[4];
    out->proto_seq = frame[5];
    out->payload_len = payload_len;
    if (payload_len > 0U) {
        memcpy(out->payload, &frame[7], payload_len);
    }
    switch ((app_ch32_proto_type_t)out->proto_type) {
        case APP_CH32_PROTO_TYPE_ACK:
            out->type = APP_CH32_LINE_PROTO_ACK;
            snprintf(out->line, sizeof(out->line),
                     "PROTO ACK cmd=0x%02X seq=%u",
                     out->proto_cmd,
                     (unsigned)out->proto_seq);
            break;
        case APP_CH32_PROTO_TYPE_NACK:
            out->type = APP_CH32_LINE_PROTO_NACK;
            snprintf(out->line, sizeof(out->line),
                     "PROTO NACK cmd=0x%02X seq=%u",
                     out->proto_cmd,
                     (unsigned)out->proto_seq);
            break;
        case APP_CH32_PROTO_TYPE_STATUS:
            out->type = APP_CH32_LINE_PROTO_STATUS;
            break;
        case APP_CH32_PROTO_TYPE_EVENT:
            out->type = APP_CH32_LINE_PROTO_EVENT;
            break;
        case APP_CH32_PROTO_TYPE_ERROR:
            out->type = APP_CH32_LINE_PROTO_ERROR;
            break;
        case APP_CH32_PROTO_TYPE_HEARTBEAT:
            out->type = APP_CH32_LINE_PROTO_HEARTBEAT;
            break;
        default:
            out->type = APP_CH32_LINE_UNKNOWN;
            break;
    }
    if ((out->type == APP_CH32_LINE_PROTO_STATUS) ||
        (out->type == APP_CH32_LINE_PROTO_EVENT) ||
        (out->type == APP_CH32_LINE_PROTO_ERROR) ||
        (out->type == APP_CH32_LINE_PROTO_HEARTBEAT)) {
        if (payload_len >= 8U) {
            out->proto_stage = (app_ch32_proto_stage_t)out->payload[0];
            out->proto_detail = out->payload[1];
            out->proto_flags = app_ch32_read_u16_le(&out->payload[2]);
            out->proto_weight_g = app_ch32_read_i32_le(&out->payload[4]);
        }
        if (out->type == APP_CH32_LINE_PROTO_ERROR) {
            snprintf(out->line, sizeof(out->line),
                     "PROTO ERR stage=%s err=%s flags=0x%04X w=%ldg",
                     app_ch32_link_proto_stage_name(out->proto_stage),
                     app_ch32_link_proto_error_name(out->proto_detail),
                     (unsigned)out->proto_flags,
                     (long)out->proto_weight_g);
        } else {
            snprintf(out->line, sizeof(out->line),
                     "PROTO %s stage=%s detail=%u flags=0x%04X w=%ldg",
                     (out->type == APP_CH32_LINE_PROTO_STATUS) ? "STS" :
                     (out->type == APP_CH32_LINE_PROTO_EVENT) ? "EVT" : "HB",
                     app_ch32_link_proto_stage_name(out->proto_stage),
                     (unsigned)out->proto_detail,
                     (unsigned)out->proto_flags,
                     (long)out->proto_weight_g);
        }
    }
    return true;
}
static void app_ch32_link_rx_task(void *arg)
{
    uint8_t ch = 0;
    char line_buf[APP_CH32_LINK_LINE_MAX] = {0};
    size_t line_len = 0;
    uint8_t proto_buf[APP_CH32_PROTO_MAX_FRAME] = {0};
    size_t proto_len = 0;
    size_t proto_expect = 0;
    bool proto_active = false;
    (void)arg;
    while (1) {
        int len = uart_read_bytes(s_ctx.uart_num, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }
        if (!proto_active) {
            if (ch == APP_CH32_PROTO_SOF1) {
                proto_active = true;
                proto_len = 1;
                proto_expect = 0;
                proto_buf[0] = ch;
                continue;
            }
        }
        if (proto_active) {
            if ((proto_len == 1U) && (ch != APP_CH32_PROTO_SOF2)) {
                proto_active = false;
                proto_len = 0;
                proto_expect = 0;
                if (line_len < sizeof(line_buf) - 1U && isprint((int)APP_CH32_PROTO_SOF1)) {
                    line_buf[line_len++] = (char)APP_CH32_PROTO_SOF1;
                }
                if (ch == '\n') {
                    if (line_len > 0U) {
                        line_buf[line_len] = '\0';
                        app_ch32_dispatch_legacy_line(line_buf);
                        line_len = 0;
                        line_buf[0] = '\0';
                    }
                } else if ((ch != '\r') && (isprint((int)ch) != 0)) {
                    if (line_len < sizeof(line_buf) - 1U) {
                        line_buf[line_len++] = (char)ch;
                    }
                }
                continue;
            }
            if (proto_len < sizeof(proto_buf)) {
                proto_buf[proto_len++] = ch;
            } else {
                proto_active = false;
                proto_len = 0;
                proto_expect = 0;
                continue;
            }
            if (proto_len == 7U) {
                const uint8_t payload_len = proto_buf[6];
                if (payload_len > APP_CH32_LINK_PROTO_MAX_PAYLOAD) {
                    proto_active = false;
                    proto_len = 0;
                    proto_expect = 0;
                    ESP_LOGW(TAG, "proto payload too large: %u", (unsigned)payload_len);
                    continue;
                }
                proto_expect = APP_CH32_PROTO_OVERHEAD + payload_len;
            }
            if ((proto_expect >= APP_CH32_PROTO_MIN_FRAME) && (proto_len == proto_expect)) {
                app_ch32_line_t msg = {0};
                if (app_ch32_parse_proto_frame(proto_buf, proto_len, &msg)) {
                    app_ch32_dispatch_msg(&msg);
                }
                proto_active = false;
                proto_len = 0;
                proto_expect = 0;
            }
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (line_len > 0U) {
                line_buf[line_len] = '\0';
                app_ch32_dispatch_legacy_line(line_buf);
                line_len = 0;
                line_buf[0] = '\0';
            }
            continue;
        }
        if (isprint((int)ch) == 0) {
            continue;
        }
        if (line_len < sizeof(line_buf) - 1U) {
            line_buf[line_len++] = (char)ch;
        } else {
            line_buf[line_len] = '\0';
            app_ch32_dispatch_legacy_line(line_buf);
            line_len = 0;
            line_buf[0] = '\0';
        }
    }
}
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
static esp_err_t app_ch32_link_send_legacy_cmd(char cmd)
{
    char frame[4] = {'@', cmd, '\n', '\0'};
    int written = uart_write_bytes(s_ctx.uart_num, frame, 3);
    ESP_RETURN_ON_FALSE(written == 3, ESP_FAIL, TAG, "uart_write_bytes legacy failed");
    ESP_LOGI(TAG, "ESP32 => CH32 legacy : %s", frame);
    return ESP_OK;
}
esp_err_t app_ch32_link_send_proto(app_ch32_proto_cmd_t cmd,
                                   const void *payload,
                                   uint8_t payload_len,
                                   uint8_t *out_seq)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(payload_len <= APP_CH32_LINK_PROTO_MAX_PAYLOAD,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "payload too large");
    uint8_t frame[APP_CH32_PROTO_MAX_FRAME] = {0};
    const uint8_t seq = ++s_ctx.next_seq;
    size_t idx = 0;
    frame[idx++] = APP_CH32_PROTO_SOF1;
    frame[idx++] = APP_CH32_PROTO_SOF2;
    frame[idx++] = APP_CH32_PROTO_VER;
    frame[idx++] = (uint8_t)APP_CH32_PROTO_TYPE_CMD;
    frame[idx++] = (uint8_t)cmd;
    frame[idx++] = seq;
    frame[idx++] = payload_len;
    if ((payload_len > 0U) && (payload != NULL)) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }
    const uint16_t crc = app_ch32_crc16_ibm(&frame[2], (size_t)(5U + payload_len));
    frame[idx++] = (uint8_t)(crc & 0xFFU);
    frame[idx++] = (uint8_t)((crc >> 8) & 0xFFU);
    int written = uart_write_bytes(s_ctx.uart_num, (const char *)frame, idx);
    ESP_RETURN_ON_FALSE(written == (int)idx, ESP_FAIL, TAG, "uart_write_bytes proto failed");
    if (out_seq != NULL) {
        *out_seq = seq;
    }
    ESP_LOGI(TAG, "ESP32 => CH32 proto: cmd=0x%02X seq=%u len=%u",
             (unsigned)cmd,
             (unsigned)seq,
             (unsigned)payload_len);
    return ESP_OK;
}
static esp_err_t app_ch32_link_wait_ack_for_cmd(char cmd, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    uint32_t slice_ms = APP_CH32_LINK_ACK_POLL_MS;
    if (slice_ms == 0U) {
        slice_ms = 50U;
    }
    while (elapsed_ms < timeout_ms) {
        uint32_t this_wait = slice_ms;
        if ((elapsed_ms + this_wait) > timeout_ms) {
            this_wait = timeout_ms - elapsed_ms;
        }
        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                               CH32_EVT_ACK,
                                               pdTRUE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(this_wait));
        elapsed_ms += this_wait;
        if ((bits & CH32_EVT_ACK) == 0U) {
            continue;
        }
        if (s_ctx.last_ack_cmd == cmd) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "ack mismatch, expect=%c actual=%c", cmd, s_ctx.last_ack_cmd);
        s_ctx.last_ack_cmd = 0;
    }
    return ESP_ERR_TIMEOUT;
}
esp_err_t app_ch32_link_init(app_ch32_line_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(!s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    s_ctx.uart_num = APP_CH32_LINK_UART_PORT;
    s_ctx.cb = cb;
    s_ctx.user_ctx = user_ctx;
    s_ctx.ready = false;
    s_ctx.proto_online = false;
    s_ctx.has_weight = false;
    s_ctx.last_ack_cmd = 0;
    s_ctx.next_seq = 0;
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
                        TAG,
                        "uart_driver_install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(s_ctx.uart_num, &uart_cfg), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(s_ctx.uart_num,
                                     APP_CH32_LINK_TX_GPIO,
                                     APP_CH32_LINK_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(APP_CH32_LINK_RX_GPIO, GPIO_PULLUP_ONLY),
                        TAG,
                        "set rx pull-up failed");
    s_ctx.event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_ctx.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group create failed");
    BaseType_t ok = xTaskCreate(app_ch32_link_rx_task, "ch32_rx", 4096, NULL, 8, &s_ctx.rx_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task create failed");
    s_ctx.inited = true;
    ESP_LOGI(TAG, "uart%d init ok, tx=%d rx=%d baud=%d",
             s_ctx.uart_num,
             APP_CH32_LINK_TX_GPIO,
             APP_CH32_LINK_RX_GPIO,
             APP_CH32_LINK_BAUD_RATE);
    return ESP_OK;
}
esp_err_t app_ch32_link_deinit(void)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_ctx.rx_task != NULL) {
        vTaskDelete(s_ctx.rx_task);
        s_ctx.rx_task = NULL;
    }
    if (s_ctx.event_group != NULL) {
        vEventGroupDelete(s_ctx.event_group);
        s_ctx.event_group = NULL;
    }
    ESP_RETURN_ON_ERROR(uart_driver_delete(s_ctx.uart_num), TAG, "uart_driver_delete failed");
    memset(&s_ctx, 0, sizeof(s_ctx));
    return ESP_OK;
}
esp_err_t app_ch32_link_send_cmd(char cmd)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    app_ch32_proto_cmd_t proto_cmd = app_ch32_legacy_cmd_to_proto(cmd);
    if (proto_cmd != APP_CH32_PROTO_CMD_NONE) {
        esp_err_t ret = app_ch32_link_send_proto(proto_cmd, NULL, 0, NULL);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "proto send failed for %c, fallback legacy", cmd);
    }
    return app_ch32_link_send_legacy_cmd(cmd);
}
esp_err_t app_ch32_link_send_cmd_and_wait_ack(char cmd, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK);
    s_ctx.last_ack_cmd = 0;
    const app_ch32_proto_cmd_t proto_cmd = app_ch32_legacy_cmd_to_proto(cmd);
    if (proto_cmd != APP_CH32_PROTO_CMD_NONE) {
        esp_err_t ret = app_ch32_link_send_proto(proto_cmd, NULL, 0, NULL);
        if (ret == ESP_OK) {
            uint32_t first_wait = APP_CH32_LINK_PROTO_ACK_FIRST_WAIT_MS;
            if (first_wait > timeout_ms) {
                first_wait = timeout_ms;
            }
            ret = app_ch32_link_wait_ack_for_cmd(cmd, first_wait);
            if (ret == ESP_OK) {
                return ESP_OK;
            }
            ESP_LOGW(TAG, "proto ack timeout for %c, fallback legacy", cmd);
        } else {
            ESP_LOGW(TAG, "proto send failed for %c: %s, fallback legacy", cmd, esp_err_to_name(ret));
        }
        xEventGroupClearBits(s_ctx.event_group, CH32_EVT_ACK);
        s_ctx.last_ack_cmd = 0;
    }
    ESP_RETURN_ON_ERROR(app_ch32_link_send_legacy_cmd(cmd), TAG, "legacy send failed");
    return app_ch32_link_wait_ack_for_cmd(cmd, timeout_ms);
}
esp_err_t app_ch32_link_probe_ready(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_ctx.ready) {
        return ESP_OK;
    }
    xEventGroupClearBits(s_ctx.event_group, CH32_EVT_READY);
    esp_err_t ret = ESP_FAIL;
    uint32_t proto_wait = APP_CH32_LINK_PROTO_PROBE_MS;
    if (proto_wait > timeout_ms) {
        proto_wait = timeout_ms;
    }
    ret = app_ch32_link_send_proto(APP_CH32_PROTO_CMD_PROBE_READY, NULL, 0, NULL);
    if (ret == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                               CH32_EVT_READY,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(proto_wait));
        if ((bits & CH32_EVT_READY) != 0U) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "proto probe timeout, fallback legacy probe");
    }
    ESP_RETURN_ON_ERROR(app_ch32_link_send_legacy_cmd('P'), TAG, "legacy probe send failed");
    EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                           CH32_EVT_READY,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return ((bits & CH32_EVT_READY) != 0U) ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t app_ch32_link_wait_ready(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ctx.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_ctx.ready) {
        return ESP_OK;
    }
    uint32_t elapsed_ms = 0;
    uint32_t slice_ms = APP_CH32_LINK_READY_RETRY_MS;
    if (slice_ms == 0U) {
        slice_ms = 300U;
    }
    while (elapsed_ms < timeout_ms) {
        uint32_t this_wait = slice_ms;
        if ((elapsed_ms + this_wait) > timeout_ms) {
            this_wait = timeout_ms - elapsed_ms;
        }
        if (app_ch32_link_probe_ready(this_wait) == ESP_OK) {
            return ESP_OK;
        }
        elapsed_ms += this_wait;
    }
    return ESP_ERR_TIMEOUT;
}
bool app_ch32_link_is_ready(void)
{
    return s_ctx.ready;
}
bool app_ch32_link_last_weight(int32_t *out_weight_g)
{
    if ((!s_ctx.has_weight) || (out_weight_g == NULL)) {
        return false;
    }
    *out_weight_g = s_ctx.last_weight_g;
    return true;
}
