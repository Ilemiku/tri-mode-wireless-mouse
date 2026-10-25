#pragma once
#include <stddef.h>
#include <stdint.h>

/* 在BLE/NVS和报告UART初始化之后调用一次，创建配网与MQTT管理对象。 */
void MouseManage_Start(void);

/* UART收到合法C2载荷后调用：更新ZE状态并投递结果，不等待网络。
 * payload只需在本次调用期间有效，队列会复制需要的数据。 */
void MouseManage_Result(const uint8_t *payload, size_t len);
