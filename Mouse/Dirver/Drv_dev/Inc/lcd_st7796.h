#ifndef MOUSE_LCD_ST7796_H
#define MOUSE_LCD_ST7796_H
#include "reg32f1.h"
#define LCD_WIDTH 480U
#define LCD_HEIGHT 320U
u16 LCD_ST7796_Init(void); /* 返回实际控制器ID，不支持的ID不会强行写初始化表 */
void LCD_ST7796_Window(u16 x1, u16 y1, u16 x2, u16 y2);
#endif
