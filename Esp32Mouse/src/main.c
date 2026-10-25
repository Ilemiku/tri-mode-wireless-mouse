#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart/uart_link.h"
#include "frame/frame_parser.h"
#include "ble/ble_hid.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "manage/mouse_manage.h"

/* 临时诊断版本。所有计数均为启动后的累计值，每秒输出一次。
 * 独立后台任务打印：即使HID库一直等发送完成，诊断也不会跟着消失。 */
typedef struct
{
    uint32_t rx_bytes, read_errors, frames, wrong_length, nonzero_frames;
    uint32_t tx_attempts, tx_ok, tx_fail, tx_not_connected;
    uint32_t last_rx_ms, send_start_ms;
    esp_err_t last_error;
    uint8_t last_raw[16], raw_length, last_report[6];
    bool sending;
} link_diag_t;

static const char *TAG = "LINK_DIAG";
static portMUX_TYPE s_link_lock = portMUX_INITIALIZER_UNLOCKED;
static link_diag_t s_link;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void hex_text(char *out, size_t capacity, const uint8_t *data, size_t length)
{
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < length && used + 3 <= capacity; ++i)
    {
        int n =
            snprintf(out + used, capacity - used, "%s%02X", i == 0 ? "" : " ", (unsigned)data[i]);
        if (n < 0 || (size_t)n >= capacity - used)
            break;
        used += (size_t)n;
    }
}

static void diagnostics_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        link_diag_t snapshot;
        ble_hid_diag_t ble;
        char raw[49], report[19];
        vTaskDelay(pdMS_TO_TICKS(1000));
        portENTER_CRITICAL(&s_link_lock);
        snapshot = s_link;
        portEXIT_CRITICAL(&s_link_lock);
        BLEHID_GetDiagnostics(&ble);
        hex_text(raw, sizeof(raw), snapshot.last_raw, snapshot.raw_length);
        hex_text(report, sizeof(report), snapshot.last_report, 6);
        ESP_LOGI(TAG,
                 "rx=%" PRIu32 " frames=%" PRIu32 " nz=%" PRIu32 " badlen=%" PRIu32
                 " readerr=%" PRIu32 " pending=%d"
                 " conn=%d attempt=%" PRIu32 " ok=%" PRIu32 " fail=%" PRIu32 " offline=%" PRIu32
                 " err=%s busy=%d wait_ms=%" PRIu32,
                 snapshot.rx_bytes, snapshot.frames, snapshot.nonzero_frames, snapshot.wrong_length,
                 snapshot.read_errors, LinkUart_Pending(), BLEHID_IsConnected(),
                 snapshot.tx_attempts, snapshot.tx_ok, snapshot.tx_fail, snapshot.tx_not_connected,
                 esp_err_to_name(snapshot.last_error), snapshot.sending,
                 snapshot.sending ? uptime_ms() - snapshot.send_start_ms : 0);
        ESP_LOGI(TAG,
                 "raw=[%s] report=[%s] auth_events=%" PRIu32 " auth_fail=%" PRIu32
                 " auth_last=%" PRId32 " writes=%" PRIu32 " last_write=0x%04x/%u/0x%04x"
                 " complete=%" PRIu32 " complete_fail=%" PRIu32 " mtu=%u"
                 " cccd=0x%04x ccc_rec=%" PRIu32 " ccc_replay=%" PRIu32,
                 raw, report, ble.auth_events, ble.auth_failures, ble.last_auth_result,
                 ble.write_events, (unsigned)ble.last_write_handle, (unsigned)ble.last_write_length,
                 (unsigned)ble.last_write_value, ble.complete_events, ble.complete_failures,
                 (unsigned)ble.mtu, (unsigned)ble.cccd_handle, ble.ccc_records, ble.ccc_replays);
    }
}

static void link_task(void)
{
    uint8_t rx_buf[32];
    frame_result_t fr = {0};

    LinkUart_Init(); // 串口初始化
    MouseManage_Start();
    FrameParser_Reset(); // 帧解析初始化
    /* ESP-IDF任务栈参数单位是字节，不是ZE端FreeRTOS的32位字。 */
    ESP_ERROR_CHECK(xTaskCreate(diagnostics_task, "link_diag", 4096, NULL, 1, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    while (1)
    {
        int n = LinkUart_Read(rx_buf, sizeof(rx_buf), 100); // 只等首字节，不等满32字节
        portENTER_CRITICAL(&s_link_lock);
        if (n > 0)
        {
            s_link.rx_bytes += (uint32_t)n;
            s_link.last_rx_ms = uptime_ms();
            s_link.raw_length = (uint8_t)(n < 16 ? n : 16);
            memcpy(s_link.last_raw, rx_buf, s_link.raw_length);
        }
        else if (n < 0)
            s_link.read_errors++;
        portEXIT_CRITICAL(&s_link_lock);
        for (int i = 0; i < n; i++)
        {
            if (!FrameParser_PutByte(rx_buf[i], &fr))
                continue;
            if (fr.len == 12 && fr.data[0] == 0xC2)
            {
                MouseManage_Result(fr.data, fr.len);
                continue;
            }
            if (fr.len != 6)
            {
                portENTER_CRITICAL(&s_link_lock);
                s_link.wrong_length++;
                portEXIT_CRITICAL(&s_link_lock);
                continue; /* 不把非六字节帧中的遗留内容当鼠标报告。 */
            }
            bool nonzero = false;
            for (unsigned j = 0; j < 6; ++j)
                nonzero |= fr.data[j] != 0;
            portENTER_CRITICAL(&s_link_lock);
            s_link.frames++;
            if (nonzero)
                s_link.nonzero_frames++;
            memcpy(s_link.last_report, fr.data, 6);
            s_link.tx_attempts++;
            s_link.sending = true;
            s_link.send_start_ms = uptime_ms();
            portEXIT_CRITICAL(&s_link_lock);

            esp_err_t result = BLEHID_SendReport(fr.data);

            portENTER_CRITICAL(&s_link_lock);
            s_link.sending = false;
            s_link.last_error = result;
            if (result == ESP_OK)
                s_link.tx_ok++;
            else if (result == ESP_ERR_INVALID_STATE)
                s_link.tx_not_connected++;
            else
                s_link.tx_fail++;
            portEXIT_CRITICAL(&s_link_lock);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "DIAG_RX_BLE_20261024: reports RX=GPIO18 TX=GPIO17 baud=115200");
    ESP_LOGI(TAG, "UART_LOWLAT_V1_20261024: wait first byte, drain available bytes");
    BLEHID_Init(); /* 启动蓝牙 HID（广播等连接） */
    link_task();
}
