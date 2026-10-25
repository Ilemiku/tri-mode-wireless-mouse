#include "uart_link.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#define LINK_UART_NUM UART_NUM_1

void LinkUart_Init(void)
{
    uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* 不再静默忽略初始化失败，否则“没有数据”无法区分驱动和传输问题。 */
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART_NUM, LINK_TX_PIN, LINK_RX_PIN, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
}

int LinkUart_Read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (len == 0)
        return 0;
    if (buf == NULL)
        return -1;

    TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    /* 正数毫秒不能意外折算成0 tick，避免短超时调用退化成忙轮询。 */
    if (timeout_ms != 0 && wait == 0)
        wait = 1;

    /* 只等待第一个字节，不等待填满调用者的整个缓冲区。
     * 原先len=32会跨多个10字节鼠标帧攒数据，增加不必要的等待。 */
    int received = uart_read_bytes(LINK_UART_NUM, buf, 1, wait);
    if (received <= 0 || len == 1)
        return received;

    /* 超时为0：只取当前已经到达的剩余字节，没有就立即返回。
     * 半帧由FrameParser保存状态，完整帧才交给BLE；不能用uart_flush丢数据。 */
    int available = uart_read_bytes(LINK_UART_NUM, buf + received, len - (size_t)received, 0);
    if (available > 0)
        received += available;
    /* 后一次读取失败也保留已取得的首字节，避免把它悄悄丢掉。 */
    return received;
}

int LinkUart_Pending(void)
{
    size_t pending = 0;
    return uart_get_buffered_data_len(LINK_UART_NUM, &pending) == ESP_OK ? (int)pending : -1;
}

int LinkUart_Write(const uint8_t *buf, size_t len)
{
    return uart_write_bytes(LINK_UART_NUM, buf, len);
}
