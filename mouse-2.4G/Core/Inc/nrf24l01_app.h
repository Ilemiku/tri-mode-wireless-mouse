#ifndef __NRF24L01_APP_H__
#define __NRF24L01_APP_H__

#include <stdint.h>

/* ============================================================
 * 应用层（dongle 业务）
 *
 * 职责：驱动初始化 → 接收模式 → 每圈收包、喂状态机、
 *       对链路状态变化做反应（打印/以后转 USB/重连）。
 *
 * 调用关系：
 *   App_Init  = 驱动 Init → Check → Set_Mode(RX) → Link_Init
 *   App_Task  = 收包 → Link_On_Packet → Link_Poll → 查状态变化
 *
 * 只调接口，不碰寄存器，不碰链路判定细节。
 * ============================================================ */

void NRF24L01_App_Init(void);
void NRF24L01_App_Task(void);

#endif /* __NRF24L01_APP_H__ */
