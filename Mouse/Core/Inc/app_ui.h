#ifndef MOUSE_APP_UI_H
#define MOUSE_APP_UI_H
#include "reg32f1.h"
typedef struct
{
    u32 cycles, flushes, dma_timeouts, dma_failures, stack_free, faults, local_requests;
    u16 lcd_id;
    u8 stage, touch_ready;
    u32 lv_free, lv_biggest;
    u8 touch_backend; /* 1=GT已识别；2=电阻式ADC探测通过，仍待校准 */
    u8 calibration_step, calibrated;
} AppUiStats;
extern volatile AppUiStats g_ui_stats;
void App_UI_Start(void);
void App_UI_Fault(void);
#endif
