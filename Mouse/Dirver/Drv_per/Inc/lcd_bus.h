#ifndef MOUSE_LCD_BUS_H
#define MOUSE_LCD_BUS_H
#include "reg32f1.h"

/* 精英ZE板固定连线：NE4=PG12、RS=A10=PG0，16位FSMC。
 * 此层只管GPIO/总线/DMA，不认识LVGL，也不认识LCD寄存器的含义。 */
typedef void (*lcd_dma_callback_t)(u8 success); /* 在DMA ISR里调用，不可阻塞 */
void LCD_Bus_Init(void);
void LCD_Bus_Command(u16 command);
void LCD_Bus_Data(u16 data);
u16 LCD_Bus_Read(void);
void LCD_Bus_Backlight(u8 on);
u8 LCD_Bus_DmaStart(const u16 *pixels, u16 count, lcd_dma_callback_t callback);
void LCD_Bus_DmaAbort(void);
extern volatile u32 g_lcd_dma_done, g_lcd_dma_errors;
#endif
