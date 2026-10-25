/* ATK-MD0350及Elite_DNF103原理图：
 * CS=PF11，SCK=PB1，MOSI=PF9，MISO=PB2，PENIRQ=PF10。
 * 电阻式触摸走独立的软件SPI，不占用传感器SPI1和射频SPI2。
 * 时序依据用户XPT2046手册与ALIENTEK touch.c；过滤/错误边界由Codex适配。
 */
#include "touch_xpt2046.h"
#include "systick.h"

volatile u32 g_xpt_samples, g_xpt_rejected;
volatile u16 g_xpt_raw_x, g_xpt_raw_y, g_xpt_temp0, g_xpt_temp1;
volatile u8 g_xpt_pressed, g_xpt_present;

static void mode(GPIO_TypeDef *port, u8 pin, u8 value)
{
    vu32 *cr = pin < 8 ? &port->CRL : &port->CRH;
    u32 shift = (pin & 7U) * 4U;
    *cr = (*cr & ~(15UL << shift)) | ((u32)value << shift);
}
static void clock(u8 high)
{
    if (high)
        GPIOB->BSRR = 1UL << 1;
    else
        GPIOB->BRR = 1UL << 1;
}
static void mosi(u8 high)
{
    if (high)
        GPIOF->BSRR = 1UL << 9;
    else
        GPIOF->BRR = 1UL << 9;
}
static void select(u8 selected)
{
    if (selected)
        GPIOF->BRR = 1UL << 11;
    else
        GPIOF->BSRR = 1UL << 11;
}
static u8 pen(void)
{
    return (GPIOF->IDR & (1UL << 10)) == 0;
}

static u16 convert(u8 command)
{
    u8 i;
    u16 value = 0;
    clock(0);
    mosi(0);
    select(1);
    for (i = 0; i < 8; i++)
    {
        mosi((command & 0x80U) != 0);
        command <<= 1;
        clock(0);
        Delay_us(1);
        clock(1);
        Delay_us(1);
    }
    Delay_us(6);
    clock(0);
    Delay_us(1);
    clock(1);
    Delay_us(1);
    clock(0); /* BUSY间隙 */
    for (i = 0; i < 16; i++)
    {
        value <<= 1;
        clock(0);
        Delay_us(1);
        clock(1);
        Delay_us(1);
        if (GPIOB->IDR & (1UL << 2))
            value |= 1U;
    }
    clock(0);
    select(0);
    return value >> 4;
}

static u16 filtered(u8 command)
{
    u16 values[5], temporary;
    u8 i, j;
    for (i = 0; i < 5; i++)
        values[i] = convert(command);
    for (i = 0; i < 4; i++)
        for (j = (u8)(i + 1); j < 5; j++)
            if (values[j] < values[i])
            {
                temporary = values[i];
                values[i] = values[j];
                values[j] = temporary;
            }
    return (u16)(((u32)values[1] + values[2] + values[3]) / 3U);
}

u8 Touch_XPT_Init(void)
{
    RCC->APB2ENR |= (1UL << 3) | (1UL << 7);
    GPIOF->BSRR = (1UL << 11) | (1UL << 10);
    GPIOF->BRR = 1UL << 9;
    GPIOB->BRR = 1UL << 1;
    mode(GPIOF, 11, GPIO_MODE_OUT_PP_50M);
    mode(GPIOB, 1, GPIO_MODE_OUT_PP_50M);
    mode(GPIOF, 9, GPIO_MODE_OUT_PP_50M);
    mode(GPIOB, 2, GPIO_MODE_IN_FLOATING);
    mode(GPIOF, 10, GPIO_MODE_IN_PUPD);
    select(0);
    /* 用内部温度通道检查串行ADC响应，不必先按屏幕。
     * 0x87/0xF7：12位单端、内部参考上电；最后用0x90恢复PD=00与笔中断。
     * 固定0/4095等无效返回不能假装“已识别到触摸”。 */
    (void)convert(0x87);
    Delay_ms(5);
    g_xpt_temp0 = filtered(0x87);
    g_xpt_temp1 = filtered(0xF7);
    (void)convert(0x90);
    g_xpt_present =
        (g_xpt_temp0 > 32 && g_xpt_temp0 < 4000 && g_xpt_temp1 > 32 && g_xpt_temp1 < 4000);
    return g_xpt_present;
}

u8 Touch_XPT_ReadRaw(u16 *x, u16 *y)
{
    u16 first_x, first_y, next_x, next_y;
    g_xpt_pressed = pen();
    if (!g_xpt_pressed)
        return 0;
    first_x = filtered(0xD0);
    first_y = filtered(0x90);
    next_x = filtered(0xD0);
    next_y = filtered(0x90);
    if (!pen() || first_x < 16 || first_x > 4079 || first_y < 16 || first_y > 4079 ||
        (first_x > next_x ? first_x - next_x : next_x - first_x) > 60 ||
        (first_y > next_y ? first_y - next_y : next_y - first_y) > 60)
    {
        g_xpt_rejected++;
        return 0;
    }
    *x = (u16)(((u32)first_x + next_x) / 2U);
    *y = (u16)(((u32)first_y + next_y) / 2U);
    g_xpt_raw_x = *x;
    g_xpt_raw_y = *y;
    g_xpt_samples++;
    return 1;
}
