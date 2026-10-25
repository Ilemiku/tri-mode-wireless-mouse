#include "nrf24l01.h"

/* ============================================================
 * 硬件层：把 SPI2_TransferByte + NCS 组装成芯片的寄存器原语
 * ============================================================ */

/* 写一个寄存器：CSN低 -> 命令(0x20+addr) -> 值 -> CSN高 */
uint8_t NRF24L01_Write_Reg(uint8_t reg, uint8_t value)
{
    uint8_t status;

    SPI2_NCS_LOW();
    status = SPI2_TransferByte(reg);   /* 发写命令，顺手收回 STATUS */
    SPI2_TransferByte(value);          /* 发值 */
    SPI2_NCS_HIGH();

    return status;
}

/* 读一个寄存器：CSN低 -> 命令(addr) -> 空数据摇时钟 -> CSN高 */
uint8_t NRF24L01_Read_Reg(uint8_t reg)
{
    uint8_t reg_val;

    SPI2_NCS_LOW();
    SPI2_TransferByte(reg);             /* 发读命令（MISO 回 STATUS，不用） */
    reg_val = SPI2_TransferByte(0xFF);  /* 空数据摇时钟，收回寄存器值 */
    SPI2_NCS_HIGH();

    return reg_val;
}

/* 读一块数据（如 5 字节地址 / 32 字节负载） */
uint8_t NRF24L01_Read_Buf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t status;
    uint8_t i;

    SPI2_NCS_LOW();
    status = SPI2_TransferByte(reg);   /* 发读命令 */
    for (i = 0; i < len; i++)
    {
        buf[i] = SPI2_TransferByte(0xFF);
    }
    SPI2_NCS_HIGH();

    return status;
}

/* 写一块数据（如 5 字节地址 / 32 字节负载） */
uint8_t NRF24L01_Write_Buf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t status;
    uint8_t i;

    SPI2_NCS_LOW();
    status = SPI2_TransferByte(reg);   /* 发写命令 */
    for (i = 0; i < len; i++)
    {
        SPI2_TransferByte(buf[i]);
    }
    SPI2_NCS_HIGH();

    return status;
}
