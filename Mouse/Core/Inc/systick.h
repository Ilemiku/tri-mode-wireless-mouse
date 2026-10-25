#ifndef __SYSTICK_H
#define __SYSTICK_H
#include "reg32f1.h"
extern volatile uint32_t g_ms_tick;
#define SystemCoreClock 72000000
#define SysTick_BASE 0xE000E010UL
#define SysTick ((SysTick_TypeDef *)SysTick_BASE)

typedef struct
{
    vu32 CTRL;
    vu32 LOAD;
    vu32 VAL;
    vu32 CALIB;
} SysTick_TypeDef;

void SystemTick_Init(void);
void Delay_ms(uint32_t ms);
void Delay_us(uint32_t us);

#endif
