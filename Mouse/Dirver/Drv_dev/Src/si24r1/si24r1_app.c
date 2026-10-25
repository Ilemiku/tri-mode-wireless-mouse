#include "si24r1_app.h"
#include "si24r1.h"
#include "si24r1_link.h"
#include "systick.h"
#include "mouse.h"
#include <stdio.h>
#include "si24r1_rtos.h"

/* ============================================================
 * 应用层实现
 * ============================================================ */

/* 发送已移到独立任务，不再每5圈丢掉前4份运动报告。 */

static SI24R1_LinkState_t s_prev_state = LINK_DOWN;

/* 2.4G 应用层帧协议（见《2.4G_应用层帧协议设计.md》）：
 *   [0..5] = USB 报告（dongle 直接透传）
 *   [6]    = type(高4位) | seq(低4位)
 *   [7]    = 状态字节：bit1:0 = DPI 档位
 * 芯片层已有 CRC16 + 自动重发，应用层不再加校验和 */
#define TYPE_DATA 0x0
static u8 s_seq = 0; /* 序号 0~15 循环：新内容才 +1，重发不改 */

/* ===== 应用层初始化：驱动 → 自检 → 发送模式 → 状态机归零 ===== */
void SI24R1_App_Init(void)
{
    printf("\r\n=== SI24R1 TX App ===\r\n");

    /* ① 驱动初始化：SPI2 Mode0 + CE/CSN 电平 */
    SI24R1_Init();

    /* ② 自检：失败=接线/供电/SPI 问题，直接卡住，别带病工作 */
    if (SI24R1_Check() == 0)
    {
        printf("SI24R1 Check OK\r\n");
    }
    else
    {
        printf("SI24R1 Check FAIL! Halt.\r\n");
        while (1)
            ;
    }

    /* ③ 进发送模式：地址/信道/速率/重发 + 上电 */
    SI24R1_Set_Mode(SI24R1_MODE_TX);

    /* ④ 状态机归零：从 LINK_DOWN 开始 */
    SI24R1_Link_Init();

    printf("TX Mode Ready, sending to dongle...\r\n");
}

/* 兼容原应用入口，任务化后的调用者使用带DPI快照的接口。 */
void SI24R1_App_Send_Report(const u8 report[6])
{
    (void)SI24R1_App_Transmit(report, Mouse_GetDPILevel());
}

u8 SI24R1_App_Transmit(const u8 report[6], u8 dpi_level)
{
    /* 这里不是任务函数，是TransferTask调用的普通组包函数。
     * report/dpi来自同一份队列消息，不重新读取InputTask后来更新的DPI。
     * 栈上的pkt在RtosSend等待期间仍有效：任务阻塞会保留它的栈。
     */
    u8 pkt[SI24R1_PLOAD_WIDTH] = {0};
    u8 i;
    u8 sta;

    /* 组帧：报告进前 6 字节，[6]=type+seq，[7]=DPI 档位 */
    for (i = 0; i < 6; i++)
    {
        pkt[i] = report[i];
    }
    pkt[6] = (u8)((TYPE_DATA << 4) | (s_seq & 0x0F));
    pkt[7] = (u8)(dpi_level & 0x03);
    s_seq++; /* 新内容才 +1；芯片自动重发不改 seq，保证"同 seq=同内容" */

    sta = SI24R1_RtosSend(pkt); /* 独立任务等待IRQ，采集任务可以继续运行。 */
    /* 把发送结果喂给状态机：TX_OK=对面活着 */
    SI24R1_Link_On_Tx_Result(sta == TX_OK, g_ms_tick);
    return sta == TX_OK;
}

/* ===== 后台任务调用：状态变化打印 + 每秒统计，不修改链路状态 ===== */
void SI24R1_App_Task(void)
{
    static u32 last_stat_ms;

    /* 超时判定由传输任务调用，避免后台打印任务同时修改链路状态。 */

    /* 状态变化才打印（边缘检测：记 prev，变了才报） */
    if (SI24R1_Link_Get_State() != s_prev_state)
    {
        s_prev_state = SI24R1_Link_Get_State();
        printf(s_prev_state == LINK_UP ? "[LINK UP]\r\n" : "[LINK DOWN]\r\n");
    }

    /* 每秒统计一次，方便看链路健康度 */
    if ((u32)(g_ms_tick - last_stat_ms) >= 1000U)
    {
        last_stat_ms = g_ms_tick;
        printf("RF: ok=%lu fail=%lu\r\n", (unsigned long)SI24R1_Link_Get_Pkt_Cnt(),
               (unsigned long)SI24R1_Link_Get_Fail_Cnt());
    }
}
