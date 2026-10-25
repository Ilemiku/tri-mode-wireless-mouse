#include "lcd_bus.h"

#define LCD_COMMAND (*(vu16 *)0x6C000000UL)
#define LCD_DATA (*(vu16 *)0x6C000800UL) /* 16位总线下CPU A11对应FSMC A10 */
#define DMA_ISR (*(vu32 *)0x40020400UL)
#define DMA_IFCR (*(vu32 *)0x40020404UL)
#define DMA_CCR (*(vu32 *)0x40020408UL) /* DMA2通道1 */
#define DMA_CNDTR (*(vu32 *)0x4002040CUL)
#define DMA_CPAR (*(vu32 *)0x40020410UL)
#define DMA_CMAR (*(vu32 *)0x40020414UL)

static volatile u8 busy;
static lcd_dma_callback_t done_callback;
volatile u32 g_lcd_dma_done, g_lcd_dma_errors;

static void pins(GPIO_TypeDef *port, u16 mask, u8 mode)
{
    u8 p;
    for (p = 0; p < 16; p++)
        if (mask & (1U << p))
        {
            vu32 *cr = p < 8 ? &port->CRL : &port->CRH;
            u32 shift = (p & 7U) * 4U;
            *cr = (*cr & ~(15UL << shift)) | ((u32)mode << shift);
        }
}

void LCD_Bus_Init(void)
{
    RCC->APB2ENR |= (1UL << 3) | (1UL << 5) | (1UL << 6) | (1UL << 8);
    RCC->AHBENR |= (1UL << 8) | (1UL << 1); /* FSMC、DMA2 */
    GPIOB->BRR = 1UL;                       /* 背光先关，防止初始化花屏 */
    pins(GPIOB, 1U, GPIO_MODE_OUT_PP_50M);
    pins(GPIOD, (3U << 0) | (3U << 4) | (7U << 8) | (3U << 14), GPIO_MODE_OUT_AF_PP_50M);
    pins(GPIOE, 0x1FFU << 7, GPIO_MODE_OUT_AF_PP_50M);
    pins(GPIOG, (1U << 0) | (1U << 12), GPIO_MODE_OUT_AF_PP_50M);
    *(vu32 *)0xA0000018UL = 0;
    *(vu32 *)0xA000001CUL = (31UL << 8) | 7U; /* 读总线留足数据建立时间 */
    *(vu32 *)0xA000011CUL = (7UL << 8) | 3U;  /* 写时序保守起步，后续测量再优化 */
    *(vu32 *)0xA0000018UL = (1UL << 14) | (1UL << 12) | (1UL << 4) | 1U;
    DMA_CCR = 0;
    DMA_IFCR = 15U;
    NVIC->IPR[56] = 7U << 4; /* 回调中会使用FreeRTOS FromISR接口，数值必须>=5 */
    NVIC->ICPR[1] = 1UL << 24;
    NVIC->ISER[1] = 1UL << 24;
}

void LCD_Bus_Command(u16 command)
{
    LCD_COMMAND = command;
}
void LCD_Bus_Data(u16 data)
{
    LCD_DATA = data;
}
u16 LCD_Bus_Read(void)
{
    return LCD_DATA;
}
void LCD_Bus_Backlight(u8 on)
{
    if (on)
        GPIOB->BSRR = 1U;
    else
        GPIOB->BRR = 1U;
}

u8 LCD_Bus_DmaStart(const u16 *pixels, u16 count, lcd_dma_callback_t callback)
{
    if (busy || !pixels || !count)
        return 0;
    DMA_CCR = 0;
    DMA_IFCR = 15U;
    NVIC->ICPR[1] = 1UL << 24;
    done_callback = callback;
    busy = 1;
    DMA_CPAR = 0x6C000800UL; /* 目的LCD数据口固定，不是逐地址递增 */
    DMA_CMAR = (u32)pixels;  /* 源RAM随每个像素递增 */
    DMA_CNDTR = count;
    /* MEM2MEM + 16位双方宽度 + MINC + DIR(内存到外设地址) + TE/TC中断。
     * 采用低DMA优先级；不会让UI任务忙等逐像素拷贝。 */
    DMA_CCR = (1UL << 14) | (1UL << 10) | (1UL << 8) | (1UL << 7) | (1UL << 4) | (1UL << 3) |
              (1UL << 1) | 1U;
    return 1;
}

void LCD_Bus_DmaAbort(void)
{
    DMA_CCR = 0;
    DMA_IFCR = 15U;
    NVIC->ICPR[1] = 1UL << 24;
    busy = 0;
    done_callback = 0;
}

void DMA2_Channel1_IRQHandler(void)
{
    u32 flags = DMA_ISR;
    if (flags & ((1UL << 1) | (1UL << 3)))
    {
        lcd_dma_callback_t callback = done_callback;
        u8 success = (flags & (1UL << 3)) == 0;
        DMA_CCR = 0;
        DMA_IFCR = 15U;
        busy = 0;
        done_callback = 0;
        if (success)
            g_lcd_dma_done++;
        else
            g_lcd_dma_errors++;
        if (callback)
            callback(success);
    }
}
