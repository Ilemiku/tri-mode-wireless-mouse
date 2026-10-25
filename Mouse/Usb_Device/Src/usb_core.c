#include "usb_core.h"
#include "usb_desc.h"

vu8 usb_setup_flag = 0; // SETUP请求标志
static u8 setup_buf[8];
static u8 g_setup_buf[8];
static u8 Zero_Buf[1] = {0x00}; // 0字节数据
vu8 usb_config_value = 0;       // 当前配置号（SET_CONFIGURATION 后生效）
static USB_SetupReq SetupReq;
static USB_SetupReq g_SetupReq; // 解析结果快照
/* ================= 请求解析与应答 ================= */
const char *USB_ReqName(u8 bmRequestType, u8 bRequest)
{
    u8 type = bmRequestType & (3U << 5); // 只看类型位（位6~5）

    if (type == USB_BMREQ_TYPE_STANDARD) // 标准请求
    {
        switch (bRequest)
        {
        case USB_REQ_GET_STATUS:
            return "GET_STATUS";
        case USB_REQ_CLEAR_FEATURE:
            return "CLEAR_FEATURE";
        case USB_REQ_SET_FEATURE:
            return "SET_FEATURE";
        case USB_REQ_SET_ADDRESS:
            return "SET_ADDRESS";
        case USB_REQ_GET_DESCRIPTOR:
            return "GET_DESCRIPTOR";
        case USB_REQ_SET_DESCRIPTOR:
            return "SET_DESCRIPTOR";
        case USB_REQ_GET_CONFIGURATION:
            return "GET_CONFIGURATION";
        case USB_REQ_SET_CONFIGURATION:
            return "SET_CONFIGURATION";
        case USB_REQ_GET_INTERFACE:
            return "GET_INTERFACE";
        case USB_REQ_SET_INTERFACE:
            return "SET_INTERFACE";
        case USB_REQ_SYNCH_FRAME:
            return "SYNCH_FRAME";
        default:
            return "STD_UNKNOWN";
        }
    }
    else if (type == USB_BMREQ_TYPE_CLASS) // 类请求
    {
        switch (bRequest)
        {
        case USB_HID_GET_REPORT:
            return "GET_REPORT";
        case USB_HID_GET_IDLE:
            return "GET_IDLE";
        case USB_HID_GET_PROTOCOL:
            return "GET_PROTOCOL";
        case USB_HID_SET_REPORT:
            return "SET_REPORT";
        case USB_HID_SET_IDLE:
            return "SET_IDLE";
        case USB_HID_SET_PROTOCOL:
            return "SET_PROTOCOL";
        default:
            return "HID_UNKNOWN";
        }
    }

    return "OTHER";
}

void USB_Ep0_Setup(void)
{
    u8 i;

    USB_HW_EpRead(0, setup_buf); // 读SETUP（固定8字节，长度从COUNT_RX自动获取）
    /* 新 SETUP 会终止上一笔控制传输，包括尚未处理的发送完成事件。 */
    USB_Ep_ClearCtrl(0, 1, 1);            // PMA 已复制，清除本次接收和上一笔发送完成标志
    usb_pending_addr = 0;                 // 取消被终止请求留下的待生效地址
    USB_Ep_SetRxStatus(0, EP_STAT_VALID); // 明确设置接收有效，不盲目翻转状态位

    SetupReq.bRequestType = setup_buf[0];
    SetupReq.bRequest = setup_buf[1];
    SetupReq.wValue = setup_buf[2] | ((u16)setup_buf[3] << 8);
    SetupReq.wIndex = setup_buf[4] | ((u16)setup_buf[5] << 8);
    SetupReq.wLength = setup_buf[6] | ((u16)setup_buf[7] << 8);

    /* 先记录请求附带的状态（地址/配置号），回包统一走下面的分支 */
    if (SetupReq.bRequest == USB_REQ_SET_ADDRESS)
    {
        usb_pending_addr = (u8)(SetupReq.wValue & 0x7F);
    }
    else if (SetupReq.bRequest == USB_REQ_SET_CONFIGURATION)
    {
        usb_config_value = (u8)(SetupReq.wValue & 0xFF);
        if (usb_config_value == 1)
        {
            USB_Ep1_Init();
        }
    }
    else if (SetupReq.bRequest == USB_REQ_CLEAR_FEATURE && SetupReq.wIndex == 0x0081)
    {
        /* 主机清理 EP1 端点状态：提前初始化，防止主机随后探测 EP1 时
           端点还是 DISABLED 无响应，导致枚举复位循环 */
        USB_Ep1_Init();
    }

    /* 需要回数据的请求优先处理 */
    if (SetupReq.bRequest == USB_REQ_GET_STATUS)
    {
        u8 status[2] = {0x00, 0x00};
        USB_HW_EpTx(0, status, 2);
    }
    else if (SetupReq.bRequest == USB_REQ_GET_CONFIGURATION)
    {
        u8 config = usb_config_value;
        USB_HW_EpTx(0, &config, 1); // 同步复制到PMA，避免丢弃volatile限定
    }
    else if (SetupReq.bRequest == USB_REQ_GET_DESCRIPTOR)
    {
        switch ((u8)(SetupReq.wValue >> 8))
        {
        /*设备描述符 / 配置描述符 / 报告描述符*/
        case USB_DESC_DEVICE:
        {
            u16 desc_len;
            const u8 *desc = USB_GetDescriptor(USB_DESC_DEVICE, &desc_len);
            USB_HW_EpTx(0, desc, (SetupReq.wLength < desc_len) ? SetupReq.wLength : desc_len);
            break;
        }
        case USB_DESC_CONFIGURATION:
        {
            u16 desc_len;
            const u8 *desc = USB_GetDescriptor(USB_DESC_CONFIGURATION, &desc_len);
            USB_HW_EpTx(0, desc, (SetupReq.wLength < desc_len) ? SetupReq.wLength : desc_len);
            break;
        }
        case USB_DESC_REPORT:
        {
            u16 desc_len;
            const u8 *desc = USB_GetDescriptor(USB_DESC_REPORT, &desc_len);
            USB_HW_EpTx(0, desc, (SetupReq.wLength < desc_len) ? SetupReq.wLength : desc_len);
            break;
        }
        case USB_DESC_STRING:
            /* 只支持 index 0（语言 ID 列表）；设备没有其他字符串 */
            if ((u8)(SetupReq.wValue & 0xFF) == 0)
            {
                USB_HW_EpTx(0, USB_LangDesc, (SetupReq.wLength < 4) ? SetupReq.wLength : 4);
            }
            break;
        default:
            break; // 暂不支持的描述符：不响应
        }
    }
    else if (SetupReq.wLength == 0)
    {
        /* SET_ADDRESS / SET_CONFIGURATION / SET_IDLE 等无数据请求： */
        USB_HW_EpTx(0, Zero_Buf, 0);
    }
    /* 其余未支持的请求暂时不响应（以后回 STALL） */

    /* 拷快照：主循环打印时数据不会被下一次SETUP覆盖 */
    for (i = 0; i < 8; i++)
    {
        g_setup_buf[i] = setup_buf[i];
    }
    g_SetupReq = SetupReq;

    usb_setup_flag = 1;
}

/* ================= 调试打印 ================= */
void USB_EventPrint(void)
{
    /* 精简调试：只打印 SETUP 包 + 解析（合并成一条 printf，避免被中断打断错位） */
    if (usb_setup_flag)
    {
        usb_setup_flag = 0;
        printf("SETUP: %02X %02X %02X %02X %02X %02X %02X %02X\r\n"
               "       [%s] %s  wValue=%04X  wIndex=%04X  wLength=%d\r\n"
               "       <%s>  OK\r\n",
               g_setup_buf[0], g_setup_buf[1], g_setup_buf[2], g_setup_buf[3], g_setup_buf[4],
               g_setup_buf[5], g_setup_buf[6], g_setup_buf[7],
               (g_SetupReq.bRequestType & (1U << 7)) ? "IN" : "OUT",
               USB_ReqName(g_SetupReq.bRequestType, g_SetupReq.bRequest), g_SetupReq.wValue,
               g_SetupReq.wIndex, g_SetupReq.wLength,
               USB_ReqName(g_SetupReq.bRequestType, g_SetupReq.bRequest));
    }
    if (usb_reset_flag)
    {
        usb_reset_flag = 0;
        printf("USB RESET!\r\n");
    }
}
