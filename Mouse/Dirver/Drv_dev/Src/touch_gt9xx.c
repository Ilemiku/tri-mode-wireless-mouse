/* 管脚与GT系列寄存器依据用户现有ALIENTEK gt9xxx/ctiic例程。
 * 新增独立错误返回、有界时钟等待、单点状态与横屏变换，由Codex编写。
 */
#include "touch_gt9xx.h"
#include "systick.h"
#include <string.h>

static u8 address;
static TouchPoint last;
static u32 last_ready;
char g_touch_product[5];
char g_touch_probe_ids[2][5];
volatile u8 g_touch_probe_ok;
volatile u32 g_touch_errors, g_touch_samples;

static void pin_mode(GPIO_TypeDef *port, u8 pin, u8 mode)
{
    vu32 *cr = pin < 8 ? &port->CRL : &port->CRH;
    u32 shift = (pin & 7U) * 4U;
    *cr = (*cr & ~(15UL << shift)) | ((u32)mode << shift);
}
/* 释放线时用输入上拉，拉低时开漏输出：F1的开漏输出本身没有内部上拉。
 * 样机未检测到外部上拉时，不能只把ODR写1就假定总线能回到高电平。
 * 这是低速板级适配，正式硬件仍推荐外部I2C上拉电阻。 */
static void sda(u8 high)
{
    if (high)
    {
        GPIOF->BSRR = 1UL << 9;
        pin_mode(GPIOF, 9, GPIO_MODE_IN_PUPD);
    }
    else
    {
        GPIOF->BRR = 1UL << 9;
        pin_mode(GPIOF, 9, GPIO_MODE_OUT_OD_50M);
    }
}
static void scl_low(void)
{
    GPIOB->BRR = 1UL << 1;
    pin_mode(GPIOB, 1, GPIO_MODE_OUT_OD_50M);
}
static u8 scl_high(void)
{
    u8 timeout = 60;
    GPIOB->BSRR = 1UL << 1;
    pin_mode(GPIOB, 1, GPIO_MODE_IN_PUPD);
    while (!(GPIOB->IDR & (1UL << 1)) && timeout--)
        Delay_us(1);
    return (GPIOB->IDR & (1UL << 1)) != 0;
}
static u8 start(void)
{
    sda(1);
    if (!scl_high())
        return 0;
    Delay_us(2);
    sda(0);
    Delay_us(2);
    scl_low();
    return 1;
}
static void stop(void)
{
    scl_low();
    sda(0);
    Delay_us(2);
    (void)scl_high();
    Delay_us(2);
    sda(1);
}
static u8 send(u8 byte)
{
    u8 i, ack;
    for (i = 0; i < 8; i++)
    {
        scl_low();
        sda((byte & 0x80U) != 0);
        byte <<= 1;
        Delay_us(2);
        if (!scl_high())
            return 0;
        Delay_us(2);
        scl_low();
    }
    sda(1);
    Delay_us(2);
    if (!scl_high())
        return 0;
    Delay_us(2);
    ack = (GPIOF->IDR & (1UL << 9)) == 0;
    scl_low();
    return ack;
}
static u8 receive(u8 *value, u8 ack)
{
    u8 i, v = 0;
    sda(1);
    for (i = 0; i < 8; i++)
    {
        scl_low();
        Delay_us(2);
        if (!scl_high())
            return 0;
        Delay_us(2);
        v = (u8)((v << 1) | ((GPIOF->IDR >> 9) & 1U));
        scl_low();
    }
    sda(!ack);
    Delay_us(2);
    if (!scl_high())
        return 0;
    Delay_us(2);
    scl_low();
    sda(1);
    *value = v;
    return 1;
}
static u8 read_reg(u16 reg, u8 *buffer, u8 length)
{
    u8 i, ok = start() && send(address << 1) && send(reg >> 8) && send((u8)reg) && start() &&
               send((address << 1) | 1U);
    for (i = 0; ok && i < length; i++)
        ok = receive(buffer + i, i + 1 < length);
    stop();
    return ok;
}
static u8 write_reg(u16 reg, u8 value)
{
    u8 ok = start() && send(address << 1) && send(reg >> 8) && send((u8)reg) && send(value);
    stop();
    return ok;
}

u8 Touch_GT_Init(void)
{
    u8 i, ok;
    RCC->APB2ENR |= (1UL << 3) | (1UL << 7);
    GPIOB->BSRR = 1UL << 1;
    GPIOF->BSRR = 1UL << 9;
    pin_mode(GPIOB, 1, GPIO_MODE_OUT_OD_50M);
    pin_mode(GPIOF, 9, GPIO_MODE_OUT_OD_50M);
    /* INT高电平选择GT的7位地址0x14，复位完成后交还为输入。 */
    GPIOF->BRR = 1UL << 11;
    GPIOF->BSRR = 1UL << 10;
    pin_mode(GPIOF, 11, GPIO_MODE_OUT_PP_50M);
    pin_mode(GPIOF, 10, GPIO_MODE_OUT_PP_50M);
    Delay_ms(10);
    GPIOF->BSRR = 1UL << 11;
    Delay_ms(20);
    pin_mode(GPIOF, 10, GPIO_MODE_IN_FLOATING);
    Delay_ms(100);
    for (i = 0; i < 2; i++)
    {
        address = i ? 0x5D : 0x14;
        memset(g_touch_product, 0, sizeof(g_touch_product));
        ok = read_reg(0x8140, (u8 *)g_touch_product, 4);
        memcpy(g_touch_probe_ids[i], g_touch_product, 5);
        if (ok)
            g_touch_probe_ok |= (u8)(1U << i);
        if (ok && (!strcmp(g_touch_product, "911") || !strcmp(g_touch_product, "9147") ||
                   !strcmp(g_touch_product, "1158") || !strcmp(g_touch_product, "9271")))
        {
            (void)write_reg(0x8040, 2);
            Delay_ms(10);
            (void)write_reg(0x8040, 0);
            last_ready = g_ms_tick;
            return 1;
        }
    }
    memset(g_touch_product, 0, sizeof(g_touch_product));
    g_touch_errors++;
    return 0;
}

void Touch_GT_Read(TouchPoint *point)
{
    u8 status, xy[4];
    if (!read_reg(0x814E, &status, 1))
    {
        g_touch_errors++;
        last.pressed = 0;
    }
    else if (status & 0x80U)
    {
        last_ready = g_ms_tick;
        if ((status & 15U) > 0 && (status & 15U) <= 10 && read_reg(0x8150, xy, 4))
        {
            u16 raw_x = (u16)(xy[0] | ((u16)xy[1] << 8)), raw_y = (u16)(xy[2] | ((u16)xy[3] << 8));
            if (raw_x < 320 && raw_y < 480)
            {
                last.x = (u16)(479U - raw_y);
                last.y = raw_x;
                last.pressed = 1;
                g_touch_samples++;
            }
            else
            {
                last.pressed = 0;
                g_touch_errors++;
            }
        }
        else
            last.pressed = 0;
        if (!write_reg(0x814E, 0))
            g_touch_errors++;
    }
    if ((u32)(g_ms_tick - last_ready) > 150)
        last.pressed = 0; /* 失联不留下永久按住 */
    *point = last;
}
