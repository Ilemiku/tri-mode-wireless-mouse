#include "si24r1.h"
#include "spi.h"
#include "systick.h"
#include <stdio.h>

/* 链路共用地址：这条 2.4G 链路只有一个"门牌号"，
 * 发送端写进 TX_ADDR（发给谁）和 RX_ADDR_P0（听 ACK），
 * 接收端写进 RX_ADDR_P0（我收谁的包）。
 * 必须与 dongle 端一字不差！ */
static const u8 SI24R1_ADDRESS[SI24R1_ADR_WIDTH] = {0x39, 0x39, 0x39, 0x39, 0x39};

/* ============================================================
 * 初始化
 * ============================================================ */
void SI24R1_Init(void)
{
    /* SPI2：Mode0（CPOL=0,CPHA=0，芯片要求）+
     *       DIV8（APB1 36MHz/8 = 4.5MHz，SI24R1 的 SPI 上限 10MHz）
     * 片选 PG7 由通用 SPI 层的表管理，这里不用管引脚 */
    SPI_Init(SPI_ID_2, SPI_MODE_0, SPI_BR_DIV8);

    SI24R1_CE = 0;          /* CE=0：配置期间待命（别乱发乱收） */
    SPI_NCS_HIGH(SPI_ID_2); /* CSN=1：释放片选 */
}

/* ============================================================
 * 自检：写 0xA5 进 TX_ADDR，读回来比对
 *
 * 为什么必须"写→读回→比对"：
 *   SPI 写操作"永远成功"——模块没接/线断时，写进去的数据只是进了空气。
 *   只有读回来的值和写进去的一致，才能证明数据真的进了芯片又出来了。
 *
 * 为什么拿 TX_ADDR 当测试台：
 *   它是 5 字节可写寄存器，能测连续多字节传输；
 *   而且 Set_Mode 后面会重写它，测试数据不会污染正式配置。
 *
 * 为什么用 0xA5：10100101，0/1 交替，最能暴露接线/位序错误；
 *   全 0 全 F 测不出来。
 * ============================================================ */
u8 SI24R1_Check(void)
{
    u8 buf_wr[SI24R1_ADR_WIDTH] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
    u8 buf_rd[SI24R1_ADR_WIDTH] = {0};
    u8 i;

    SI24R1_Write_Buf(SI24R1_WRITE_REG + TX_ADDR, buf_wr, SI24R1_ADR_WIDTH);
    SI24R1_Read_Buf(TX_ADDR, buf_rd, SI24R1_ADR_WIDTH);

    for (i = 0; i < SI24R1_ADR_WIDTH; i++)
    {
        if (buf_rd[i] != buf_wr[i])
        {
            return 1; /* 对不上 = 通信异常（全0xFF=没供电/没接，全0x00=MISO短路） */
        }
    }
    return 0;
}

/* ============================================================
 * 模式配置
 *
 * 规则：所有配置都在 CE=0 期间写；CONFIG 最后写（bit1=PWR_UP 上电）；
 *       写完等 tPOR≈2ms 让晶振稳定，再决定 CE 怎么用。
 *
 * 发送端特别的一行：
 *   RX_ADDR_P0 也写同一地址 —— 芯片发完包要转接收去听 ACK，
 *   P0 是听 ACK 的"耳朵"，必须和 TX_ADDR 一样，否则永远收不到回执。
 * ============================================================ */
void SI24R1_Set_Mode(u8 mode)
{
    SI24R1_CE = 0; /* 配置前先关 CE */

    if (mode == SI24R1_MODE_RX)
    {
        /* 接收端配置：地址 + 自动应答 + 信道 + 负载宽度 + 速率功率 + 上电 */
        SI24R1_Write_Buf(SI24R1_WRITE_REG + RX_ADDR_P0, (u8 *)SI24R1_ADDRESS, SI24R1_ADR_WIDTH);
        SI24R1_Write_Reg(SI24R1_WRITE_REG + EN_AA, 0x01);                  /* 管道0自动应答 */
        SI24R1_Write_Reg(SI24R1_WRITE_REG + EN_RXADDR, 0x01);              /* 只开管道0 */
        SI24R1_Write_Reg(SI24R1_WRITE_REG + RF_CH, 0x27);                  /* 信道 39 */
        SI24R1_Write_Reg(SI24R1_WRITE_REG + RX_PW_P0, SI24R1_PLOAD_WIDTH); /* 8字节 */
        SI24R1_Write_Reg(SI24R1_WRITE_REG + RF_SETUP, 0x0F);               /* 2Mbps+0dBm */
        SI24R1_Write_Reg(SI24R1_WRITE_REG + CONFIG, 0x0F);                 /* 上电+16位CRC+接收 */
        Delay_ms(2);                                                       /* 等 tPOR */
        SI24R1_CE = 1;                                                     /* CE=1 持续监听 */
    }
    else if (mode == SI24R1_MODE_TX)
    {
        /* 发送端配置：TX_ADDR + RX_ADDR_P0(听ACK) + 自动应答 + 重发 + 信道 + 速率 + 上电 */
        SI24R1_Write_Buf(SI24R1_WRITE_REG + TX_ADDR, (u8 *)SI24R1_ADDRESS, SI24R1_ADR_WIDTH);
        SI24R1_Write_Buf(SI24R1_WRITE_REG + RX_ADDR_P0, (u8 *)SI24R1_ADDRESS, SI24R1_ADR_WIDTH);
        SI24R1_Write_Reg(SI24R1_WRITE_REG + EN_AA, 0x01);
        SI24R1_Write_Reg(SI24R1_WRITE_REG + EN_RXADDR, 0x01);
        SI24R1_Write_Reg(SI24R1_WRITE_REG + SETUP_RETR, 0x1A); /* 500µs × 10次 */
        SI24R1_Write_Reg(SI24R1_WRITE_REG + RF_CH, 0x27);
        SI24R1_Write_Reg(SI24R1_WRITE_REG + RF_SETUP, 0x0F);
        SI24R1_Write_Reg(SI24R1_WRITE_REG + CONFIG, 0x0E); /* 上电+16位CRC+发送 */
        Delay_ms(2);
        /* CE 保持低 —— 发送是"扣扳机"，由 Send 每次拉高触发 */
    }
}

/* ============================================================
 * 发送事务拆分：下面三个函数只操作硬件，不知道FreeRTOS任务是什么。
 *
 * 五步：
 *   ① CE=0：先关"扳机"
 *   ② 写负载进 TX FIFO（固定 8 字节）
 *   ③ CE=1：拉高 ≥10µs 触发发射（芯片自动走完 发射→等ACK→重发）
 *   ④ 等 IRQ 引脚拉低 = 芯片干完了（成功或重发耗尽）
 *   ⑤ 读 STATUS 判断成败 → 清标志 → 失败时清掉残留 FIFO
 *
 * 为什么等 IRQ 引脚而不是定时猜：芯片自己知道什么时候干完，
 * 拉低那一刻就是结果，比延时可靠。
 * 等待策略由调用方决定：裸机兼容入口忙等；RTOS适配层让任务阻塞等待。
 * ============================================================ */
void SI24R1_TxPrepare(void)
{
    SI24R1_CE = 0;                                     // 先停止触发下一笔发送，再清上一笔残留
    SI24R1_Write_Reg(SI24R1_WRITE_REG + STATUS, 0x70); // 状态bit6/5/4写1清除
    SI24R1_Write_Reg(FLUSH_TX, 0xFF);                  // FLUSH_TX是命令，后面的FF不是“清空值”
}

void SI24R1_TxStart(u8 *buf)
{
    SI24R1_Write_Buf(WR_TX_PLOAD, buf, SI24R1_PLOAD_WIDTH); // 数据先放进芯片TX FIFO
    SI24R1_CE = 1; // 发射由射频芯片继续执行，不需要CPU逐个无线bit发送
}

u8 SI24R1_TxFinish(void)
{
    /* 被任务通知叫醒仅表示要检查结果；这里的STATUS才决定成功/重发耗尽。 */
    u8 sta;
    SI24R1_CE = 0;
    sta = SI24R1_Read_Reg(STATUS);                    /* 读状态 */
    SI24R1_Write_Reg(SI24R1_WRITE_REG + STATUS, sta); /* 清标志（写1清除） */

    if (sta & MAX_TX)
    {
        SI24R1_Write_Reg(FLUSH_TX, 0xFF); /* 重发耗尽：清掉 TX FIFO 残留 */
        return MAX_TX;
    }
    if (sta & TX_OK)
    {
        return TX_OK; /* 发送成功 = 收到 ACK = 对面还活着 */
    }
    SI24R1_Write_Reg(FLUSH_TX, 0xFF);
    return 0xFF;
}

/* 裸机兼容接口也增加超时；RTOS发送改由独立适配层完成。 */
u8 SI24R1_Send(u8 *buf)
{
    u32 start;
    SI24R1_TxPrepare();
    SI24R1_TxStart(buf);
    start = g_ms_tick;
    while (SI24R1_IRQ != 0 && (u32)(g_ms_tick - start) < 20U)
    {
    }
    return SI24R1_TxFinish();
}

/* ============================================================
 * 接收一包（轮询 STATUS）
 * 返回 0 = 收到，1 = 没有
 * 注意：RX 模式下 CE 必须保持 1（Set_Mode 已置），这里只查岗不碰 CE
 * ============================================================ */
u8 SI24R1_Recv(u8 *buf)
{
    u8 sta;

    sta = SI24R1_Read_Reg(STATUS);
    SI24R1_Write_Reg(SI24R1_WRITE_REG + STATUS, sta); /* 清标志 */

    if (sta & RX_OK)
    {
        SI24R1_Read_Buf(RD_RX_PLOAD, buf, SI24R1_PLOAD_WIDTH);
        SI24R1_Write_Reg(FLUSH_RX, 0xFF); /* 读完清 FIFO，腾地方给下一包 */
        return 0;
    }
    return 1;
}
