#include "nrf24l01.h"
#include "nrf24l01_regs.h"

/* 链路共用地址：两端必须一字不差 */
static const uint8_t NRF_ADDRESS[5] = {0x39, 0x39, 0x39, 0x39, 0x39};

/* ============================================================
 * 初始化：SPI 速度 + 控制脚初始电平
 * （引脚模式由 CubeMX/HAL 配置，这里只做电平和速度）
 * ============================================================ */
void NRF24L01_Init(void)
{
    /* CSN(PB12) 必须在这里配成推挽输出！
     * CubeMX 只生成了 PB0(IRQ)/PB1(CE)，没生成 PB12；
     * 老版跑通的 24l01.c 就是在驱动里自己配的。
     * 不配的话 CSN 是复位后的浮空输入，SPI2_NCS_LOW/HIGH 写 ODR
     * 根本驱动不了引脚 → 芯片选不中 → 配置写不进去 → 永远收不到包 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin   = GPIO_PIN_12;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SPI 速度降到 DIV8（4.5MHz），SI24R1 的 SPI 上限 10MHz */
    __HAL_SPI_DISABLE(&hspi2);
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    HAL_SPI_Init(&hspi2);
    __HAL_SPI_ENABLE(&hspi2);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);  /* CE = 0：配置期间待命 */
    SPI2_NCS_HIGH();                                       /* CSN = 1：释放片选 */
}

/* ============================================================
 * 自检：写已知值进 TX_ADDR，读回来比对
 * 返回 0 = 模块正常，1 = 通信异常
 * ============================================================ */
uint8_t NRF24L01_Check(void)
{
    uint8_t buf_wr[5] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
    uint8_t buf_rd[5] = {0};
    uint8_t i;

    NRF24L01_Write_Buf(NRF_WRITE_REG + TX_ADDR, buf_wr, TX_ADR_WIDTH);
    NRF24L01_Read_Buf(TX_ADDR, buf_rd, TX_ADR_WIDTH);

    for (i = 0; i < TX_ADR_WIDTH; i++)
    {
        if (buf_rd[i] != buf_wr[i])
        {
            return 1;   /* 对不上 = 通信异常 */
        }
    }
    return 0;           /* 全对上 = 模块正常 */
}

/* ============================================================
 * 模式配置：写完整参数并上电
 * ============================================================ */
void NRF24L01_Set_Mode(uint8_t mode)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);  /* 配置前先关 CE */

    if (mode == NRF_MODE_RX)
    {
        NRF24L01_Write_Buf(NRF_WRITE_REG + RX_ADDR_P0, (uint8_t *)NRF_ADDRESS, RX_ADR_WIDTH);
        NRF24L01_Write_Reg(NRF_WRITE_REG + EN_AA,      0x01);   /* 自动应答 */
        NRF24L01_Write_Reg(NRF_WRITE_REG + EN_RXADDR,  0x01);   /* 允许管道0 */
        NRF24L01_Write_Reg(NRF_WRITE_REG + RF_CH,      0x27);   /* 信道 39 */
        NRF24L01_Write_Reg(NRF_WRITE_REG + RX_PW_P0,   RX_PLOAD_WIDTH); /* 负载宽度 */
        NRF24L01_Write_Reg(NRF_WRITE_REG + RF_SETUP,   0x0F);   /* 2Mbps + 0dBm */
        NRF24L01_Write_Reg(NRF_WRITE_REG + CONFIG,     0x0F);   /* 上电+16位CRC+接收 */

        HAL_Delay(2);                                          /* 等 tPOR */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);    /* CE=1 持续监听 */
    }
    else if (mode == NRF_MODE_TX)
    {
        NRF24L01_Write_Buf(NRF_WRITE_REG + TX_ADDR,    (uint8_t *)NRF_ADDRESS, TX_ADR_WIDTH);
        NRF24L01_Write_Buf(NRF_WRITE_REG + RX_ADDR_P0, (uint8_t *)NRF_ADDRESS, RX_ADR_WIDTH); /* 听 ACK */
        NRF24L01_Write_Reg(NRF_WRITE_REG + EN_AA,      0x01);
        NRF24L01_Write_Reg(NRF_WRITE_REG + EN_RXADDR,  0x01);
        NRF24L01_Write_Reg(NRF_WRITE_REG + SETUP_RETR, 0x1A);   /* 500µs × 10次 */
        NRF24L01_Write_Reg(NRF_WRITE_REG + RF_CH,      0x27);   /* 信道 39，和 RX 一致 */
        NRF24L01_Write_Reg(NRF_WRITE_REG + RF_SETUP,   0x0F);   /* 2Mbps + 0dBm */
        NRF24L01_Write_Reg(NRF_WRITE_REG + CONFIG,     0x0E);   /* 上电+16位CRC+发送 */

        HAL_Delay(2);                                          /* 等 tPOR */
        /* CE 保持低，由 Send 拉高 ≥10µs 触发发射 */
    }
}

/* ============================================================
 * 发送一包（固定长度，阻塞等完成）
 * ============================================================ */
void NRF24L01_Send(uint8_t *buf)
{
    uint8_t sta;

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);   /* CE 拉低 */
    NRF24L01_Write_Buf(WR_TX_PLOAD, buf, TX_PLOAD_WIDTH);   /* 写负载进 TX FIFO */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);     /* CE 拉高触发发射 */

    while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) != GPIO_PIN_RESET);  /* 等 IRQ 拉低 */

    sta = NRF24L01_Read_Reg(STATUS);
    NRF24L01_Write_Reg(NRF_WRITE_REG + STATUS, sta);        /* 清标志 */

    if (sta & MAX_TX)
    {
        NRF24L01_Write_Reg(FLUSH_TX, 0xFF);                 /* 重发耗尽，清残留 */
    }
}

/* ============================================================
 * 接收一包（轮询 STATUS）
 * 返回 0 = 收到，1 = 没有
 * ============================================================ */
uint8_t NRF24L01_Recv(uint8_t *buf)
{
    uint8_t sta;

    sta = NRF24L01_Read_Reg(STATUS);
    NRF24L01_Write_Reg(NRF_WRITE_REG + STATUS, sta);        /* 清标志（写1清除） */

    if (sta & RX_OK)
    {
        NRF24L01_Read_Buf(RD_RX_PLOAD, buf, RX_PLOAD_WIDTH);
        NRF24L01_Write_Reg(FLUSH_RX, 0xFF);
        return 0;
    }
    return 1;
}
