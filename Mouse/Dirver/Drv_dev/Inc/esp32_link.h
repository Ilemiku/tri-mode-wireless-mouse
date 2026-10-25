#ifndef __ESP32_LINK_H__
#define __ESP32_LINK_H__

#include "reg32f1.h"

/* ============================================================
 * ESP32 蓝牙桥链路层（ZE → ESP32 → BLE → 电脑）
 *
 * 职责：把 6 字节鼠标报告组帧，经 UART3 发给 ESP32。
 * 与 si24r1_app.c 对仗：2.4G 有应用层，蓝牙桥也有自己的应用层。
 * ============================================================ */

void ESP32_Link_Send_Report(const u8 report[6]); /* 6字节报告 → [AA][55][06][d0..d5][SUM] → UART3 */
/* UI和远程命令共用的设置入口：1=查询，2=设置DPI。返回1只代表已排队。 */
u8 Mouse_SettingsRequest(u32 id, u8 operation, u16 dpi);
typedef struct
{
    u32 updated_ms, local_id;
    u16 dpi;
    u8 reg, mode, local_status;
} MouseSettingsState;
u8 Mouse_SettingsLocalRequest(u32 id, u8 operation, u16 dpi);
void Mouse_SettingsGetState(MouseSettingsState *state); /* 读快照，不访问SPI */
void Mouse_SettingsInit(void);
void Mouse_SettingsInputPoll(void); /* Input任务：解析命令、独占传感器执行 */
void Mouse_SettingsTxPoll(void);    /* Transfer任务：独占UART发送结果 */

#endif /* __ESP32_LINK_H__ */
