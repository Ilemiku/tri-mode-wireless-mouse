#ifndef __SPI_H
#define __SPI_H

#include "reg32f1.h"

/* ============================================================
 * 通用 SPI 层（迷你 HAL）
 *
 * 调用方只认 SPI_Id_t，不碰寄存器指针；
 * 每个实例的"个性"（时钟总线/使能位/片选引脚）集中在 spi.c 的表里。
 * ============================================================ */

/* 实例 ID */
typedef enum
{
    SPI_ID_1 = 0,
    SPI_ID_2,
    SPI_ID_3, /* 高密度才有，先占位 */
    SPI_ID_NUM
} SPI_Id_t;

/* 四种模式，枚举值直接对应 CPOL/CPHA 组合 */
typedef enum
{
    SPI_MODE_0 = 0, /* CPOL=0 CPHA=0 */
    SPI_MODE_1,     /* CPOL=0 CPHA=1 */
    SPI_MODE_2,     /* CPOL=1 CPHA=0 */
    SPI_MODE_3      /* CPOL=1 CPHA=1 */
} SPI_Mode_t;

/* 分频 */
typedef enum
{
    SPI_BR_DIV2 = 0,
    SPI_BR_DIV4,
    SPI_BR_DIV8,
    SPI_BR_DIV16,
    SPI_BR_DIV32,
    SPI_BR_DIV64,
    SPI_BR_DIV128,
    SPI_BR_DIV256
} SPI_Br_t;

/* ===== 通用接口 ===== */
void SPI_Init(SPI_Id_t id, SPI_Mode_t mode, SPI_Br_t br);
u8 SPI_Transfer(SPI_Id_t id, u8 tx); /* 全双工收发一字节 */
void SPI_NCS_LOW(SPI_Id_t id);       /* 片选拉低：选中 */
void SPI_NCS_HIGH(SPI_Id_t id);      /* 片选拉高：释放 */

#endif
