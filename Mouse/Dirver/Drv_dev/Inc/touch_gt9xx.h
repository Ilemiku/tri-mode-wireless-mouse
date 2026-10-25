#ifndef MOUSE_TOUCH_GT9XX_H
#define MOUSE_TOUCH_GT9XX_H
#include "reg32f1.h"
typedef struct
{
    u16 x, y;
    u8 pressed;
} TouchPoint;
/* 板上触摸总线固定PB1/PF9，不是STM32硬件I2C复用脚，故用有超时的软件I2C。 */
u8 Touch_GT_Init(void);
void Touch_GT_Read(TouchPoint *point);
extern char g_touch_product[5];
extern volatile u32 g_touch_errors, g_touch_samples;
extern char g_touch_probe_ids[2][5];
extern volatile u8 g_touch_probe_ok;
#endif
