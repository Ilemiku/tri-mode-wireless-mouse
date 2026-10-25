#ifndef __SI24R1_H__
#define __SI24R1_H__

#include "reg32f1.h"

/* ============================================================
 * SI24R1（寄存器兼容 nRF24L01+）2.4G 驱动 —— 芯片定义层
 *
 * 硬件：SPI2(PB13/14/15) + CE=PG8 + CSN=PG7 + IRQ=PG6
 *
 * 链路参数（必须与 dongle 端一字不差！）：
 *   地址 {39 39 39 39 39}、信道 0x27(39)、2Mbps、
 *   8 字节负载、16位CRC、自动应答
 *
 * 这个文件 = 芯片的"字典"：
 *   命令字/寄存器地址/状态位 全在这里，驱动和上层都靠它。
 * ============================================================ */

/* ---- 命令字 ----
 * 第一个字节永远是命令。寄存器命令的结构：
 *   bit5 = 1 → 写寄存器(W_REGISTER=0x20) / 0 → 读寄存器(R_REGISTER=0x00)
 *   低 5 位 = 寄存器地址
 * 所以代码里到处是 SI24R1_WRITE_REG + 寄存器地址 */
#define SI24R1_READ_REG 0x00
#define SI24R1_WRITE_REG 0x20
#define RD_RX_PLOAD 0x61 /* 读 RX 负载（最多32字节） */
#define WR_TX_PLOAD 0xA0 /* 写 TX 负载 */
#define FLUSH_TX 0xE1    /* 清 TX FIFO */
#define FLUSH_RX 0xE2    /* 清 RX FIFO */
#define REUSE_TX_PL 0xE3 /* 重发上一包 */
#define NOP 0xFF         /* 空操作（摇时钟用） */

/* ---- 寄存器地址（"信箱编号"，查手册 Register Map） ---- */
#define CONFIG 0x00     /* 总开关：PWR_UP/CRC/收发模式 */
#define EN_AA 0x01      /* 自动应答使能（每 bit 一个管道） */
#define EN_RXADDR 0x02  /* 接收地址使能 */
#define SETUP_AW 0x03   /* 地址宽度 3/4/5 字节 */
#define SETUP_RETR 0x04 /* 自动重发：间隔+次数 */
#define RF_CH 0x05      /* 射频信道 */
#define RF_SETUP 0x06   /* 速率+发射功率 */
#define STATUS 0x07     /* 状态：RX_DR/TX_DS/MAX_RT */
#define OBSERVE_TX 0x08 /* 发射检测：丢包/重发计数 */
#define CD 0x09         /* 载波检测 */
#define RX_ADDR_P0 0x0A /* 管道0接收地址（5字节） */
#define RX_ADDR_P1 0x0B
#define RX_ADDR_P2 0x0C
#define RX_ADDR_P3 0x0D
#define RX_ADDR_P4 0x0E
#define RX_ADDR_P5 0x0F
#define TX_ADDR 0x10  /* 发射地址（包发给谁） */
#define RX_PW_P0 0x11 /* 管道0负载宽度（1~32） */
#define RX_PW_P1 0x12
#define RX_PW_P2 0x13
#define RX_PW_P3 0x14
#define RX_PW_P4 0x15
#define RX_PW_P5 0x16
#define FIFO_STATUS 0x17 /* FIFO 状态 */
#define DYNPD 0x1C       /* 动态负载使能 */
#define FEATURE 0x1D     /* 特性：动态负载/ACK负载等 */

/* ---- STATUS 位 ----
 * 芯片的"工作汇报"，每次 SPI 事务第一个字节都会免费带一份。
 * 注意：RX_DR/TX_DS/MAX_RT 是"写1清除"——读完要写回去，否则标志一直挂着 */
#define STATUS_TX_FULL (1U << 0)
#define STATUS_MAX_RT (1U << 4) /* 重发耗尽 = 发送失败 */
#define STATUS_TX_DS (1U << 5)  /* 发送完成 = 收到 ACK */
#define STATUS_RX_DR (1U << 6)  /* 收到数据 */
#define MAX_TX STATUS_MAX_RT
#define TX_OK STATUS_TX_DS
#define RX_OK STATUS_RX_DR

/* ---- 地址/负载宽度 ----
 * 负载宽度必须和 dongle 的 RX_PW_P0 一致（都是 8） */
#define SI24R1_ADR_WIDTH 5
#define SI24R1_PLOAD_WIDTH 8

/* ---- 模式 ---- */
#define SI24R1_MODE_TX 0
#define SI24R1_MODE_RX 1

/* ---- CE / IRQ 控制脚 ----
 * CE 和 IRQ 不是寄存器，是普通 GPIO，不走 SPI：
 *   CE   = 发射/接收的"工作开关"（MCU 拉高/拉低）
 *   IRQ  = 芯片"举手报告"（开漏输出，拉低=有事）
 * 用位带操作直接读写 ODR/IDR，比调函数快，也是寄存器工程的风格。
 * 位带原理：把"一个 bit"映射到"一个 32 位地址"，读写那个地址=读写那个脚 */
#define BITBAND_PERIPH(addr, bitnum)                                                               \
    (*(volatile uint32_t *)(0x42000000UL + (((uint32_t)(addr) - 0x40000000UL) * 32) +              \
                            ((bitnum) * 4)))
#define PGout(n) BITBAND_PERIPH(&(GPIOG->ODR), (n))
#define PGin(n) BITBAND_PERIPH(&(GPIOG->IDR), (n))
#define SI24R1_CE PGout(8) /* PG8 驱动 CE */
#define SI24R1_IRQ PGin(6) /* PG6 读 IRQ */

/* ---- 驱动接口（应用层只用这些） ---- */
void SI24R1_Init(void);
u8 SI24R1_Check(void);
void SI24R1_Set_Mode(u8 mode);
u8 SI24R1_Send(u8 *buf);
void SI24R1_TxPrepare(void);
void SI24R1_TxStart(u8 *buf);
u8 SI24R1_TxFinish(void);
u8 SI24R1_Recv(u8 *buf);

/* ---- 内部原语（hw.c 实现，core.c 调用） ---- */
u8 SI24R1_Write_Reg(u8 reg, u8 value);
u8 SI24R1_Read_Reg(u8 reg);
u8 SI24R1_Read_Buf(u8 reg, u8 *buf, u8 len);
u8 SI24R1_Write_Buf(u8 reg, u8 *buf, u8 len);

#endif /* __SI24R1_H__ */
