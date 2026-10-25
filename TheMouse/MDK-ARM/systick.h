#ifndef __SYSTICK_H
#define __SYSTICK_H
#include <stdio.h>
extern volatile uint32_t g_ms_tick;
void SysTick_Init(void);
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
#endif