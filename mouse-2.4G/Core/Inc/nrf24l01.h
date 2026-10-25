#ifndef __NRF24L01_H__
#define __NRF24L01_H__

#include "spi.h"
#include <stdint.h>

/* 工作模式 */
#define NRF_MODE_TX  0
#define NRF_MODE_RX  1

/* ===== 应用层接口（对外只露这些） ===== */
void     NRF24L01_Init(void);
uint8_t  NRF24L01_Check(void);
void     NRF24L01_Set_Mode(uint8_t mode);   /* TX/RX */
void     NRF24L01_Send(uint8_t *buf);       
uint8_t  NRF24L01_Recv(uint8_t *buf);       

/* ===== 驱动内部原语（hw.c 实现，core.c 调用） ===== */
uint8_t NRF24L01_Write_Reg(uint8_t reg, uint8_t value);
uint8_t NRF24L01_Read_Reg(uint8_t reg);
uint8_t NRF24L01_Read_Buf(uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t NRF24L01_Write_Buf(uint8_t reg, uint8_t *buf, uint8_t len);

#endif /* __NRF24L01_H__ */
