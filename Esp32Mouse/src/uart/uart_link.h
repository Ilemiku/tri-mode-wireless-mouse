#ifndef __UART_LINK_H__
#define __UART_LINK_H__

#include <stdint.h>
#include <stddef.h>
#include "driver/gpio.h"

#define LINK_TX_PIN GPIO_NUM_17 /* ESP32 发 → 接 STM32 PB11(RX) */
#define LINK_RX_PIN GPIO_NUM_18 /* ESP32 收 ← 接 STM32 PB10(TX) */
#define LINK_BAUD 115200

void LinkUart_Init(void);
/* len是容量，不是必须等满的数量。只对首字节等待timeout_ms，随后立即取现有数据。
 * 返回实际字节数；0=本次没有数据，负数=错误。允许返回半帧，由上层状态机续接。 */
int LinkUart_Read(uint8_t *buf, size_t len, uint32_t timeout_ms);
int LinkUart_Pending(void); /* 诊断用：驱动中尚未被主循环取走的接收字节数 */
int LinkUart_Write(const uint8_t *buf, size_t len);

#endif
