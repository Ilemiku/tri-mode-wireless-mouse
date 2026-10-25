#include "spi.h"
#include "gpio.h"

/* ============================================================
 * 实例私有数据表：一个 SPI 的所有"个性"都集中在这里
 * 加新实例 = 加一行
 * ============================================================ */
typedef struct
{
    SPI_TypeDef *reg;      /* 寄存器基址 */
    u8 clk_bus;            /* 0=APB1, 1=APB2 */
    u8 clk_bit;            /* 时钟使能位 */
    GPIO_TypeDef *cs_port; /* 片选端口 */
    u16 cs_pin;            /* 片选引脚 */
} SPI_Inst_t;

static const SPI_Inst_t g_spi_inst[SPI_ID_NUM] = {
    /* SPI1：PWM3360 传感器（Mode3），片选 PE2 */
    {SPI1, 1, 12, GPIOE, GPIO_PIN_2},
    /* SPI2：SI24R1 2.4G（Mode0），片选 PG7 */
    {SPI2, 0, 14, GPIOG, GPIO_PIN_7},
};

void SPI_Init(SPI_Id_t id, SPI_Mode_t mode, SPI_Br_t br)
{
    const SPI_Inst_t *p = &g_spi_inst[id];
    u32 cr1;

    /* ① 时钟：查表决定 APB1/APB2 */
    if (p->clk_bus == 0)
        Reg_Set(RCC->APB1ENR, 1U << (p->clk_bit));
    else
        Reg_Set(RCC->APB2ENR, 1U << (p->clk_bit));

    /* ② CR1：主模式 + 软NSS + 分频 + 模式（bit0→CPHA, bit1→CPOL） */
    cr1 = ((u32)br << 3) | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    if (mode & 0x01)
        cr1 |= SPI_CR1_CPHA;
    if (mode & 0x02)
        cr1 |= SPI_CR1_CPOL;

    p->reg->CR1 = cr1;
    p->reg->CR1 |= SPI_CR1_SPE;

    /* ③ 片选默认释放（高） */
    GPIO_Set(p->cs_port, p->cs_pin);
}

u8 SPI_Transfer(SPI_Id_t id, u8 tx)
{
    SPI_TypeDef *spi = g_spi_inst[id].reg;

    while (!(spi->SR & SPI_SR_TXE))
        ;                  /* 等发件箱空 */
    *(vu8 *)&spi->DR = tx; /* 写数据触发传输 */
    while (!(spi->SR & SPI_SR_RXNE))
        ; /* 等收件箱有信 */
    return *(vu8 *)&spi->DR;
}

void SPI_NCS_LOW(SPI_Id_t id)
{
    GPIO_Reset(g_spi_inst[id].cs_port, g_spi_inst[id].cs_pin);
}

void SPI_NCS_HIGH(SPI_Id_t id)
{
    GPIO_Set(g_spi_inst[id].cs_port, g_spi_inst[id].cs_pin);
}
