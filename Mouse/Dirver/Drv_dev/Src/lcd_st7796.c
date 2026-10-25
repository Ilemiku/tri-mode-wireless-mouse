/* ST7796初始化参数参考用户现有正点原子精英F103板例程lcd_ex.c。
 * 原参数版权：Copyright (c) 2020-2032 广州市星翼电子科技有限公司(ALIENTEK)。
 * 本文件的寄存器总线适配、紧凑表及界面集成由Codex编写。
 */
#include "lcd_st7796.h"
#include "lcd_bus.h"
#include "systick.h"

typedef struct
{
    u8 command, length;
    u8 data[14];
} LcdSetting;
static const LcdSetting init_table[] = {
    {0x36, 1, {0xA8}}, /* 横屏480x320，与板级触摸坐标变换对应 */
    {0x3A, 1, {0x55}}, /* RGB565，16位一个像素 */
    {0xF0, 1, {0xC3}},
    {0xF0, 1, {0x96}},
    {0xB4, 1, {0x01}},
    {0xB6, 2, {0x0A, 0xA2}},
    {0xB7, 1, {0xC6}},
    {0xB9, 2, {0x02, 0xE0}},
    {0xC0, 2, {0x80, 0x16}},
    {0xC1, 1, {0x19}},
    {0xC2, 1, {0xA7}},
    {0xC5, 1, {0x16}},
    {0xE8, 8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}},
    {0xE0,
     14,
     {0xF0, 0x07, 0x0D, 0x04, 0x05, 0x14, 0x36, 0x54, 0x4C, 0x38, 0x13, 0x14, 0x2E, 0x34}},
    {0xE1,
     14,
     {0xF0, 0x10, 0x14, 0x0E, 0x0C, 0x08, 0x35, 0x44, 0x4C, 0x26, 0x10, 0x12, 0x2C, 0x32}},
    {0xF0, 1, {0x3C}},
    {0xF0, 1, {0x69}}};

u16 LCD_ST7796_Init(void)
{
    u16 id;
    u32 i;
    u8 j;
    LCD_Bus_Init();
    Delay_ms(10);
    LCD_Bus_Command(0xD3);
    (void)LCD_Bus_Read();
    (void)LCD_Bus_Read();
    id = (u16)(LCD_Bus_Read() << 8);
    id |= LCD_Bus_Read();
    if (id != 0x7796)
        return id;
    LCD_Bus_Command(0x11);
    Delay_ms(120);
    for (i = 0; i < sizeof(init_table) / sizeof(init_table[0]); i++)
    {
        LCD_Bus_Command(init_table[i].command);
        for (j = 0; j < init_table[i].length; j++)
            LCD_Bus_Data(init_table[i].data[j]);
    }
    Delay_ms(120);
    LCD_Bus_Command(0x21);
    LCD_Bus_Command(0x29);
    LCD_Bus_Backlight(1);
    return id;
}

void LCD_ST7796_Window(u16 x1, u16 y1, u16 x2, u16 y2)
{
    LCD_Bus_Command(0x2A);
    LCD_Bus_Data(x1 >> 8);
    LCD_Bus_Data(x1 & 255);
    LCD_Bus_Data(x2 >> 8);
    LCD_Bus_Data(x2 & 255);
    LCD_Bus_Command(0x2B);
    LCD_Bus_Data(y1 >> 8);
    LCD_Bus_Data(y1 & 255);
    LCD_Bus_Data(y2 >> 8);
    LCD_Bus_Data(y2 & 255);
    LCD_Bus_Command(0x2C);
}
