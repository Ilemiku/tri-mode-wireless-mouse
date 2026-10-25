#ifndef SI24R1_RTOS_H
#define SI24R1_RTOS_H
#include "reg32f1.h"
void SI24R1_RtosIrqInit(void);  // 调度器启动前，配置PG6下降沿与NVIC；暂不开放EXTI事件
u8 SI24R1_RtosSend(u8 *packet); // 仅传输任务调用；等待会阻塞本任务，返回TX_OK/MAX_TX/0xFF
#endif
