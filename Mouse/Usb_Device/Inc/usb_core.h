#ifndef __USB_CORE_H__
#define __USB_CORE_H__

#include "usb_hw.h"

const char *USB_ReqName(u8 bmRequestType, u8 bRequest);

void USB_EventPrint(void);
void USB_Ep0_Setup(void);

extern vu8 usb_setup_flag;   // SETUP请求标志
extern vu8 usb_pending_addr; // SET_ADDRESS待生效地址（定义在 usb_hw.c）
extern vu8 usb_config_value; // 当前配置号（定义在 usb_core.c）

#endif
