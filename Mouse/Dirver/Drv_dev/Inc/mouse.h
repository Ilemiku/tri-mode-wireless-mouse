#ifndef __MOUSE_H__
#define __MOUSE_H__

#include "reg32f1.h"

/* 按键号（对应报告描述符 Usage 1~4） */
#define MOUSE_BTN_LEFT 1
#define MOUSE_BTN_RIGHT 2
#define MOUSE_BTN_MIDDLE 3
#define MOUSE_BTN_BACK 4

typedef enum
{
    MOUSE_MODE_USB = 0, /* 有线：USB HID 上报 */
    MOUSE_MODE_2_4G,    /* 2.4G：SI24R1 → dongle */
    MOUSE_MODE_BLE,     /* 蓝牙：UART3 → ESP32 */
    MOUSE_MODE_NUM
} mouse_mode_t;

void Mouse_ModeCycle(void);
mouse_mode_t Mouse_ModeGet(void);
void Mouse_ModeSet(const u8 *report); // 按当前模式路由到对应通道

void Mouse_Init(void);                    // 清按键位图、设默认 DPI
void Mouse_SetButton(u8 btn, u8 pressed); // btn=1~4, pressed=1按下/0松开
void Mouse_DPICycle(void);                // 切下一档 DPI
void Mouse_Report(u8 report[6]);          // 读传感器 + 组 6 字节报告
u8 Mouse_GetDPILevel(void);               // 当前 DPI 档位下标（2.4G 状态字节用）
/* 由Input任务调用：写Config1并读回，成功才更新档位。 */
u8 Mouse_ApplyDPI(u16 dpi);
u16 Mouse_ReadActualDPI(u8 *raw);

#endif
