#ifndef __NRF24L01_REGS_H__
#define __NRF24L01_REGS_H__

/* ============================================================
 * nRF24L01+ / SI24R1 寄存器与命令定义
 * 给驱动、中间层、应用层共用
 * ============================================================ */

/* ---- 命令字 ---- */
#define NRF_READ_REG    0x00    /* 读寄存器：0x00 + 地址 */
#define NRF_WRITE_REG   0x20    /* 写寄存器：0x20 + 地址 */
#define RD_RX_PLOAD     0x61    /* 读 RX 负载（最多 32 字节） */
#define WR_TX_PLOAD     0xA0    /* 写 TX 负载 */
#define FLUSH_TX        0xE1    /* 清 TX FIFO */
#define FLUSH_RX        0xE2    /* 清 RX FIFO */
#define REUSE_TX_PL     0xE3    /* 重发上一包 */
#define NOP             0xFF    /* 空操作（读 STATUS 用） */

/* ---- 寄存器地址 ---- */
#define CONFIG          0x00    /* 配置：PWR_UP/CRC/模式 */
#define EN_AA           0x01    /* 自动应答使能（每 bit 一个管道） */
#define EN_RXADDR       0x02    /* 接收地址使能（每 bit 一个管道） */
#define SETUP_AW        0x03    /* 地址宽度：3/4/5 字节 */
#define SETUP_RETR      0x04    /* 自动重发：间隔+次数 */
#define RF_CH           0x05    /* 射频信道 */
#define RF_SETUP        0x06    /* 速率+发射功率 */
#define STATUS          0x07    /* 状态：RX_DR/TX_DS/MAX_RT */
#define OBSERVE_TX      0x08    /* 发射检测：丢包/重发计数 */
#define CD              0x09    /* 载波检测 */
#define RX_ADDR_P0      0x0A    /* 管道0接收地址（5字节） */
#define RX_ADDR_P1      0x0B    /* 管道1接收地址（5字节） */
#define RX_ADDR_P2      0x0C    /* 管道2（只有最低字节） */
#define RX_ADDR_P3      0x0D    /* 管道3（只有最低字节） */
#define RX_ADDR_P4      0x0E    /* 管道4（只有最低字节） */
#define RX_ADDR_P5      0x0F
#define TX_ADDR         0x10    /* 发射地址 */
#define RX_PW_P0        0x11    /* 管道0负载宽度（1~32） */
#define RX_PW_P1        0x12
#define RX_PW_P2        0x13
#define RX_PW_P3        0x14
#define RX_PW_P4        0x15
#define RX_PW_P5        0x16
#define FIFO_STATUS     0x17    /* FIFO 状态 */
#define DYNPD           0x1C    /* 动态负载使能 */
#define FEATURE         0x1D    /* 特性：动态负载/ACK负载等 */

/* ---- STATUS 位 ---- */
#define STATUS_TX_FULL  (1U << 0)
#define STATUS_MAX_RT   (1U << 4)   /* 重发耗尽 */
#define STATUS_TX_DS    (1U << 5)   /* 发送完成 */
#define STATUS_RX_DR    (1U << 6)   /* 收到数据 */

/* ---- 常用取值别名 ---- */
#define MAX_TX          STATUS_MAX_RT
#define TX_OK           STATUS_TX_DS
#define RX_OK           STATUS_RX_DR

/* ---- 地址/负载宽度 ---- */
#define TX_ADR_WIDTH    5
#define RX_ADR_WIDTH    5
#define TX_PLOAD_WIDTH  8
#define RX_PLOAD_WIDTH  8

#endif /* __NRF24L01_REGS_H__ */
