#ifndef SPI_HW_H
#define SPI_HW_H

#include <stdint.h>

void SPI_Init(void);              /* 初始化硬件 SPI 总线 + 设备 */
uint8_t SPI_Transfer(uint8_t tx); /* 硬件 SPI 收发一字节，返回收到的字节 */

void SPI_CS_HIGH(void); /* 片选拉高（失能） */
void SPI_CS_LOW(void);  /* 片选拉低（使能） */

#endif
