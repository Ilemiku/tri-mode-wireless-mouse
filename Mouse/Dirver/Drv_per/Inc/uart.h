#ifndef __UART_H
#define __UART_H
#include "reg32f1.h"
void UART1_EnableAsync(void);
#include "gpio.h"
#define SystemCoreClock 72000000 // 系统时钟频率 PCLK1

void UART1_Init(void);
void UART1_SendByte(u8 ch);

void UART3_Init(void);
void UART3_SendByte(u8 ch);
void UART3_SendBuf(const u8 *buf, u16 len);
void UART3_EnableRx(void);
int UART3_TryRead(u8 *byte);
extern volatile u32 g_uart3_rx_errors;

#endif
