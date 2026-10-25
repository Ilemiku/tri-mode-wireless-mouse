#ifndef __USB_HW_H
#define __USB_HW_H

#include "reg32f1.h"
#include "systick.h"
#include <stdio.h>

#define USB_PMA_BASE 0x40006000UL
#define USB_PMA ((USB_BufTable_TypeDef *)USB_PMA_BASE)
#define EP_STAT_VALID 3U
#define EP_STAT_NAK 2U

extern vu8 usb_reset_flag; // USB复位标志（定义在 usb_hw.c）

/* 在 Keil 观察窗口展开 g_usb_ep0_trace，可查看初始化和总线复位记录。
 * 中断中只保存快照和计数，不调用 printf，避免打印影响枚举时序。
 */
typedef struct
{
    u32 init_count;             // EP0 初始化累计次数
    u32 reset_count;            // USB 总线复位处理累计次数
    u32 setup_count;            // 收到的 SETUP 请求累计次数
    u32 rx_count;               // EP0 接收完成累计次数
    u32 tx_count;               // EP0 发送完成累计次数
    u16 init_before;            // 初始化写入前的 EP0R
    u16 init_write;             // 实际写入 EP0R 的值，包含所需翻转位
    u16 init_after;             // 初始化写入后的 EP0R 快照
    u16 reset_istr_before;      // 清除 RESET 前的 ISTR
    u16 reset_istr_after_clear; // 清除 RESET 后的 ISTR
    u16 reset_ep0_after;        // 本次总线复位中重新初始化后的 EP0R
} USB_Ep0Trace;
extern volatile USB_Ep0Trace g_usb_ep0_trace;

typedef struct
{
    vu16 ADDR_TX;
    vu16 _pad0; // 发送缓冲地址
    vu16 COUNT_TX;
    vu16 _pad1; // 发送字节数
    vu16 ADDR_RX;
    vu16 _pad2; // 接收缓冲地址
    vu16 COUNT_RX;
    vu16 _pad3; // 接收缓冲大小
} USB_BufTable_TypeDef;

typedef struct
{
    vu8 bRequestType; // 请求类型
    vu8 bRequest;     // 请求码
    vu16 wValue;      // 值
    vu16 wIndex;      // 索引
    vu16 wLength;
} USB_SetupReq;

/* ================= bmRequestType 位域 ================= */

/* 方向（位7） */
#define USB_BMREQ_DIR_OUT (0U << 7) // 主机 -> 设备
#define USB_BMREQ_DIR_IN (1U << 7)  // 设备 -> 主机

/* 类型（位6~5） */
#define USB_BMREQ_TYPE_STANDARD (0U << 5) // 标准请求
#define USB_BMREQ_TYPE_CLASS (1U << 5)    // 类请求（HID）
#define USB_BMREQ_TYPE_VENDOR (2U << 5)   // 厂商自定义

/* 对象（位4~0） */
#define USB_BMREQ_RECIP_DEVICE (0U << 0)    // 设备
#define USB_BMREQ_RECIP_INTERFACE (1U << 0) // 接口
#define USB_BMREQ_RECIP_ENDPOINT (2U << 0)  // 端点

/* 常用完整组合 */
#define USB_BMREQ_GET_DESCRIPTOR                                                                   \
    (USB_BMREQ_DIR_IN | USB_BMREQ_TYPE_STANDARD | USB_BMREQ_RECIP_DEVICE)                    // 0x80
#define USB_BMREQ_SET (USB_BMREQ_DIR_OUT | USB_BMREQ_TYPE_STANDARD | USB_BMREQ_RECIP_DEVICE) // 0x00
#define USB_BMREQ_HID_GET                                                                          \
    (USB_BMREQ_DIR_IN | USB_BMREQ_TYPE_CLASS | USB_BMREQ_RECIP_INTERFACE) // 0xA1
#define USB_BMREQ_HID_SET                                                                          \
    (USB_BMREQ_DIR_OUT | USB_BMREQ_TYPE_CLASS | USB_BMREQ_RECIP_INTERFACE) // 0x21

/* ================= bRequest 标准请求号 ================= */

#define USB_REQ_GET_STATUS 0
#define USB_REQ_CLEAR_FEATURE 1
#define USB_REQ_SET_FEATURE 3
#define USB_REQ_SET_ADDRESS 5
#define USB_REQ_GET_DESCRIPTOR 6 // GET_DESCRIPTOR
#define USB_REQ_SET_DESCRIPTOR 7
#define USB_REQ_GET_CONFIGURATION 8 // GET_CONFIGURATION
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE 10
#define USB_REQ_SET_INTERFACE 11
#define USB_REQ_SYNCH_FRAME 12

/* ================= 描述符类型（wValue 高字节） ================= */

#define USB_DESC_DEVICE 1        /// 设备描述符
#define USB_DESC_CONFIGURATION 2 // 配置描述符
#define USB_DESC_STRING 3
#define USB_DESC_INTERFACE 4
#define USB_DESC_ENDPOINT 5
#define USB_DESC_HID 0x21
#define USB_DESC_REPORT 0x22
#define USB_DESC_PHYSICAL 0x23

/* ================= HID 类请求号 ================= */

#define USB_HID_GET_REPORT 1
#define USB_HID_GET_IDLE 2
#define USB_HID_GET_PROTOCOL 3
#define USB_HID_SET_REPORT 9
#define USB_HID_SET_IDLE 10
#define USB_HID_SET_PROTOCOL 11

void USB_HW_Init(void);
void USB_HW_Clock_Init(void);
void USB_HW_PowerOn(void);
void USB_HW_EpRx(u8 ep);
void USB_HW_EpTx(u8 ep, const u8 *buf, u16 len);
u8 USB_HW_EpTryTx(u8 ep, const u8 *buf, u16 len);
u16 USB_HW_EpRead(u8 ep, u8 *buf);
void USB_Ep0_Init(void);
void USB_Ep1_Init(void);
void USB_LP_CAN1_RX0_IRQHandler(void);
void USB_Ep_Toggle(u8 ep, u16 toggle_bits);
void USB_Ep_ClearCtrl(u8 ep, u8 clear_rx, u8 clear_tx);
void USB_Ep_SetRxStatus(u8 ep, u16 status);
#endif
