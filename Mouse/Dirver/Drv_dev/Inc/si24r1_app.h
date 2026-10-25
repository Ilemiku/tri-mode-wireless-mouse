#ifndef __SI24R1_APP_H__
#define __SI24R1_APP_H__

#include "reg32f1.h"

/* ============================================================
 * 应用层（ZE 发送端业务）
 *
 * 调用链：
 *   app ──▶ link(状态机) ──▶ core(驱动) ──▶ hw(SPI原语) ──▶ SPI层
 *
 * 职责：初始化、组包发送、对链路状态变化做反应。
 * 只调接口，不碰寄存器，不碰链路判定细节。
 * ============================================================ */

void SI24R1_App_Init(void);
void SI24R1_App_Send_Report(const u8 report[6]); /* 6字节鼠标报告 → 8字节包发出 */
u8 SI24R1_App_Transmit(const u8 report[6], u8 dpi_level);
void SI24R1_App_Task(void);

#endif /* __SI24R1_APP_H__ */
