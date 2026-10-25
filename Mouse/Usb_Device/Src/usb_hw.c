#include "usb_hw.h"
#include "usb_core.h"

/* ================= 内部状态与缓冲 ================= */
vu8 usb_reset_flag = 0; // USB复位标志
vu8 usb_pending_addr;   // SET_ADDRESS待生效地址
volatile USB_Ep0Trace g_usb_ep0_trace;

/* EPnR 分别包含普通读写字段、写 0 清除的完成标志、写 1 翻转的状态字段。 */
#define USB_EP_RW_MASK 0x070FU
#define USB_EP_CTR_MASK 0x8080U
#define USB_EP_STAT_TX_MASK 0x0030U
#define USB_EP_STAT_RX_MASK 0x3000U
#define USB_EP_TOGGLE_MASK 0x7070U
/* 32位对齐存储：每个16位字占2个cell（隔一个用），64字节缓冲=64个cell */
static u16 usb_ep_tx_addr[2] = {0x20, 0xA0};
static u16 usb_ep_rx_addr[2] = {0x60, 0xA8};
/* ================= 初始化 ================= */
void USB_HW_PowerOn(void) /* 复位 USB */
{
    /* 模拟收发器启动期间保持协议引擎复位，待电路稳定后再释放。 */
    USB->CNTR = USB_CNTR_FRES; // PDWN=0：退出断电；FRES=1：保持复位

    Delay_us(10); // 等 tSTARTUP

    USB->CNTR = 0; // 清FRES：释放复位

    USB->ISTR = 0; // 清除所有中断标志
}

void USB_HW_Clock_Init(void)
{
    RCC_APB1ENR_USBEN(); // USB时钟使能

    RCC_APB1RSTR_USBRST(); // USB时钟复位

    RCC->APB1RSTR &= ~(1U << 23); // 复位取消
}

void USB_HW_Init(void)
{
    USB_HW_Clock_Init();

    USB_HW_PowerOn();

    usb_config_value = 0;
    usb_pending_addr = 0;
    USB_Ep0_Init();

    USB->CNTR = USB_CNTR_RESETM | USB_CNTR_CTRM;

    NVIC->ISER[0] |= (1U << 20); // 使能USB中断
}

/* ================= PMA 端点操作 ================= */
/* 只翻转 STAT/DTOG，完全不碰 CTR。
   toggle_bits 里写1的位才会翻转，写0的位不动。 */
void USB_Ep_Toggle(u8 ep, u16 toggle_bits)
{
    vu16 *epr;
    u16 reg;
    if (ep >= 2)
        return;
    epr = (vu16 *)USB + 2 * ep;
    reg = *epr;
    /* 两个 CTR 位都写 1 保持，避免清掉读取寄存器后刚到达的完成事件。 */
    *epr = (u16)((reg & USB_EP_RW_MASK) | USB_EP_CTR_MASK | (toggle_bits & USB_EP_TOGGLE_MASK));
}

/* 仅修改选中的 STAT 字段，保持 DATA 翻转位和两个传输完成标志。
 * STAT 写 1 才翻转，因此需要写入的翻转位 = 当前状态 XOR 目标状态。
 */
static void USB_Ep_SetStatus(u8 ep, u16 mask, u16 desired)
{
    vu16 *epr = (vu16 *)USB + 2 * ep;
    u16 current = *epr;
    *epr = (u16)((current & USB_EP_RW_MASK) | USB_EP_CTR_MASK | ((current ^ desired) & mask));
}

void USB_Ep_SetRxStatus(u8 ep, u16 status)
{
    if (ep >= 2 || status > 3U)
        return;
    USB_Ep_SetStatus(ep, USB_EP_STAT_RX_MASK, (u16)(status << 12));
}

/* 只清 CTR_RX/CTR_TX，完全不碰 STAT/DTOG。
   clear_rx=1 清 CTR_RX，clear_tx=1 清 CTR_TX。 */
void USB_Ep_ClearCtrl(u8 ep, u8 clear_rx, u8 clear_tx)
{
    vu16 *epr;
    u16 reg;
    if (ep >= 2)
        return;
    epr = (vu16 *)USB + 2 * ep;
    reg = (u16)((*epr & USB_EP_RW_MASK) | USB_EP_CTR_MASK);

    if (clear_rx)
    {
        reg &= ~(1U << 15); // 写0清CTR_RX
    }
    if (clear_tx)
    {
        reg &= ~(1U << 7); // 写0清CTR_TX
    }
    *epr = reg;
}

void USB_HW_EpTx(u8 ep, const u8 *buf, u16 len)
{
    (void)USB_HW_EpTryTx(ep, buf, len);
}

u8 USB_HW_EpTryTx(u8 ep, const u8 *buf, u16 len)
{
    vu16 *pma = (vu16 *)USB_PMA_BASE;
    vu16 *epr;
    u16 reg;
    u16 addr;
    u16 i;

    if (ep >= 2)
    {
        return 0;
    }

    epr = (vu16 *)USB + 2 * ep;
    reg = *epr;

    /* 忙则跳过：只对 EP1 这类主循环轮询端点生效，
       EP0 控制传输由中断驱动，必须无条件开门回包 */
    if (ep != 0 && ((reg >> 4) & 0x03) == EP_STAT_VALID)
    {
        return 0;
    }

    addr = usb_ep_tx_addr[ep];

    /* 每个16位字存到32位对齐的cell：addr, addr+2, addr+4 ... */
    for (i = 0; i < len / 2; i++)
    {
        pma[addr + 2 * i] = buf[2 * i] | ((u16)buf[2 * i + 1] << 8);
    }
    if (len & 1) // 奇数长度：末字节独占一个字
    {
        pma[addr + 2 * (len / 2)] = buf[len - 1];
    }

    USB_PMA[ep].ADDR_TX = addr; // 告诉硬件缓冲区在哪
    USB_PMA[ep].COUNT_TX = len; // 告诉硬件发多少字节

    /* 明确设为 VALID，不再假定原状态一定是 NAK；重复设置也不会关门。 */
    USB_Ep_SetStatus(ep, USB_EP_STAT_TX_MASK, (u16)(EP_STAT_VALID << 4));
    return 1;
}

void USB_HW_EpRx(u8 ep)
{
    if (ep >= 2)
    {
        return;
    }

    USB_PMA[ep].ADDR_RX = usb_ep_rx_addr[ep];
    USB_PMA[ep].COUNT_RX = (1U << 15) | (1U << 10); // 接收缓冲 64 字节

    USB_Ep_SetRxStatus(ep, EP_STAT_VALID);
}

u16 USB_HW_EpRead(u8 ep, u8 *buf)
{
    vu16 *pma = (vu16 *)USB_PMA_BASE;
    u16 addr;
    u16 len;
    u16 i;

    if (ep >= 2)
    {
        return 0;
    }

    addr = usb_ep_rx_addr[ep];
    len = USB_PMA[ep].COUNT_RX & ((1U << 10) - 1); // 实际收到的字节数

    /* 硬件按32位对齐存放：字i在cell addr+2*i */
    for (i = 0; i < len / 2; i++)
    {
        buf[2 * i] = pma[addr + 2 * i] & 0xFF;
        buf[2 * i + 1] = (pma[addr + 2 * i] >> 8) & 0xFF;
    }
    if (len & 1) // 奇数长度：末字节独占一个字
    {
        buf[len - 1] = pma[addr + 2 * (len / 2)] & 0xFF;
    }

    return len;
}

/* ================= 端点初始化 ================= */
/* 仅用于端点初始化：DATA 翻转位归零，并清除旧的传输完成标志。
 * 运行期修改收发状态应使用 USB_Ep_SetStatus，以保留传输完成标志。
 */
static void USB_Ep_InitRegister(u8 ep, u16 target)
{
    vu16 *epr = (vu16 *)USB + 2 * ep;
    u16 before = *epr;
    u16 write_value = (u16)(((before ^ target) & USB_EP_TOGGLE_MASK) | (target & USB_EP_RW_MASK));
    *epr = write_value;
    if (ep == 0)
    {
        g_usb_ep0_trace.init_before = before;
        g_usb_ep0_trace.init_write = write_value;
        g_usb_ep0_trace.init_after = *epr;
        g_usb_ep0_trace.init_count++;
    }
}

void USB_Ep0_Init(void)
{
    USB->BTABLE = 0; // 端点表在 PMA 开头

    USB_PMA->ADDR_TX = usb_ep_tx_addr[0];
    USB_PMA->COUNT_TX = 0;
    USB_PMA->ADDR_RX = usb_ep_rx_addr[0];
    USB_PMA->COUNT_RX = (1U << 15) | (1U << 10); // 接收最大长度64字节

    USB_Ep_InitRegister(0, (u16)((EP_STAT_VALID << 12) | (EP_STAT_NAK << 4) | (1U << 9)));
    USB->DADDR = (1U << 7); // EF=1，地址0 —— 必须有！
}

void USB_Ep1_Init(void)
{
    vu16 *pma = (vu16 *)USB_PMA_BASE;
    u16 addr = usb_ep_tx_addr[1]; // 0xA0

    /* 预置一帧全 0 报告（6 字节 = 3 个半字，隔一个 cell 存） */
    pma[addr] = 0x0000;
    pma[addr + 2] = 0x0000;
    pma[addr + 4] = 0x0000;

    USB_PMA[1].ADDR_TX = usb_ep_tx_addr[1];
    USB_PMA[1].COUNT_TX = 6;

    /* 一开始就 VALID：主机第一次轮询就有数据可拿，不用等主循环开门
       EA=1（bit0）：F1 用 EPnR 的 EA 字段匹配令牌里的端点号，
       不写 1 的话主机轮询 EP1 时硬件不响应，STAT_TX 会一直卡在 VALID */
    USB_Ep_InitRegister(1, (u16)((EP_STAT_NAK << 12) | (EP_STAT_VALID << 4) | (3U << 9) | 1U));
}

/* ================= USB 中断 ================= */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    if (USB->ISTR & (1U << 10)) // RESET
    {
        g_usb_ep0_trace.reset_istr_before = USB->ISTR;
        /* 与 ST HAL 顺序一致：先清 RESET 标志，再重新开启默认控制端点。 */
        USB->ISTR = (u16) ~(1U << 10);
        g_usb_ep0_trace.reset_istr_after_clear = USB->ISTR;
        usb_config_value = 0;
        usb_pending_addr = 0;
        usb_setup_flag = 0;
        USB_Ep_InitRegister(1, 1U); /* 复位后先禁用 EP1，等主机重新配置再启用。 */
        USB_Ep0_Init();
        g_usb_ep0_trace.reset_ep0_after = USB->EP0R;
        g_usb_ep0_trace.reset_count++;
        usb_reset_flag = 1;
        return; /* 新到达的传输完成事件留到下一次中断重新读取处理。 */
    }
    if (USB->ISTR & (1U << 15)) // CTR
    {
        u16 ep_id = USB->ISTR & 0x0F; // 哪个端点的传输完成（位3:0）

        if (ep_id == 0)
        {
            u16 ep0 = USB->EP0R;
            if (ep0 & (1U << 15)) // CTR_RX
            {
                g_usb_ep0_trace.rx_count++;
                if (ep0 & (1U << 11)) // SETUP
                {
                    g_usb_ep0_trace.setup_count++;
                    /* 先复制 SETUP 数据，再清 CTR_RX 和重新开启接收。 */
                    USB_Ep0_Setup(); // 读+开门+解析+回包+置标志
                }
                else
                {
                    USB_Ep_ClearCtrl(0, 1, 0);
                    USB_HW_EpRx(0); // 重新开RX
                }
                return; /* 避免用旧快照里的 CTR_TX 提交新 SETUP 的待生效地址。 */
            }

            if (ep0 & (1U << 7)) // CTR_TX
            {
                g_usb_ep0_trace.tx_count++;
                USB_Ep_ClearCtrl(0, 0, 1); // 清CTR_TX，保留CTR_RX
                if (usb_pending_addr)
                {
                    USB->DADDR = (usb_pending_addr | (1U << 7));
                    usb_pending_addr = 0;
                }
            }
        }
        else if (ep_id == 1)
        {
            u16 ep1 = USB->EP1R;
            if (ep1 & (1U << 7)) // CTR_TX：EP1 发完，清掉解除冻结
            {
                USB_Ep_ClearCtrl(1, 0, 1);
            }
            if (ep1 & (1U << 15)) // CTR_RX：EP1 未配 RX，一般不会触发
            {
                USB_Ep_ClearCtrl(1, 1, 0);
            }
        }
    }
}
