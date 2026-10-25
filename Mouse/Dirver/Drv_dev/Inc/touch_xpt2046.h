#ifndef MOUSE_TOUCH_XPT2046_H
#define MOUSE_TOUCH_XPT2046_H
#include "reg32f1.h"
/* 兼容XPT2046/HR2046的电阻式触摸底层，只提供12位ADC原始量。
 * 没有数字芯片ID；探测成功不等于坐标已校准，不能直接把ADC数当屏幕像素。 */
u8 Touch_XPT_Init(void);
u8 Touch_XPT_ReadRaw(u16 *x, u16 *y);
extern volatile u32 g_xpt_samples, g_xpt_rejected;
extern volatile u16 g_xpt_raw_x, g_xpt_raw_y, g_xpt_temp0, g_xpt_temp1;
extern volatile u8 g_xpt_pressed, g_xpt_present;
#endif
