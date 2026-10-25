#ifndef __USB_DESC_H__
#define __USB_DESC_H__
#include "reg32f1.h"

const u8 *USB_GetDescriptor(u8 type, u16 *len);
extern const u8 USB_LangDesc[4]; // 字符串描述符 index 0（语言 ID 列表）

#endif
