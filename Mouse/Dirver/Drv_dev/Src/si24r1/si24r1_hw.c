#include "si24r1.h"
#include "spi.h"

/* ============================================================
 * 硬件层：把"SPI 字节传输"翻译成"芯片的寄存器读写协议"
 *
 * 每次事务都是同一个套路：
 *   CSN 拉低（选中芯片）
 *   → 发命令字节（读/写 + 寄存器地址）
 *   → 发/收数据字节
 *   → CSN 拉高（释放芯片，芯片在上升沿锁存命令）
 *
 * 底下用的是通用 SPI 层（SPI_ID_2）：
 *   SPI_Transfer(id, byte)  = 全双工收发一字节（同时进一出）
 *   SPI_NCS_LOW/HIGH(id)    = 片选（PG7）

 * ============================================================ */

/* 写一个寄存器：
 *   CSN低 → 发 0x20|addr（写命令）→ 发值 → CSN高
 *   第一个字节芯片会顺便把 STATUS 回给你，所以返回值 = STATUS */
u8 SI24R1_Write_Reg(u8 reg, u8 value)
{
    u8 status;

    SPI_NCS_LOW(SPI_ID_2);
    status = SPI_Transfer(SPI_ID_2, reg);
    SPI_Transfer(SPI_ID_2, value);
    SPI_NCS_HIGH(SPI_ID_2);

    return status;
}

/* 读一个寄存器：
 *   CSN低 → 发 addr（读命令）→ 发 0xFF 摇时钟，收回寄存器值 → CSN高
 *   为什么要发 0xFF：时钟由主机产生，你不发东西芯片就没机会把数据推回来 */
u8 SI24R1_Read_Reg(u8 reg)
{
    u8 reg_val;

    SPI_NCS_LOW(SPI_ID_2);
    SPI_Transfer(SPI_ID_2, reg);
    reg_val = SPI_Transfer(SPI_ID_2, 0xFF);
    SPI_NCS_HIGH(SPI_ID_2);

    return reg_val;
}

/* 读一块数据（如 5 字节地址）：发一次命令，循环收 len 字节 */
u8 SI24R1_Read_Buf(u8 reg, u8 *buf, u8 len)
{
    u8 status;
    u8 i;

    SPI_NCS_LOW(SPI_ID_2);
    status = SPI_Transfer(SPI_ID_2, reg);
    for (i = 0; i < len; i++)
    {
        buf[i] = SPI_Transfer(SPI_ID_2, 0xFF);
    }
    SPI_NCS_HIGH(SPI_ID_2);

    return status;
}

/* 写一块数据（如 5 字节地址 / 8 字节负载） */
u8 SI24R1_Write_Buf(u8 reg, u8 *buf, u8 len)
{
    u8 status;
    u8 i;

    SPI_NCS_LOW(SPI_ID_2);
    status = SPI_Transfer(SPI_ID_2, reg);
    for (i = 0; i < len; i++)
    {
        SPI_Transfer(SPI_ID_2, buf[i]);
    }
    SPI_NCS_HIGH(SPI_ID_2);

    return status;
}
