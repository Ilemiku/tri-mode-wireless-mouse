#include "nrf24l01_app.h"
#include "nrf24l01.h"
#include "nrf24l01_regs.h"
#include "nrf24l01_link.h"
#include "main.h"
#include "usbd_hid.h"
#include <stdio.h>

/* USB 设备句柄：usb_device.c 里定义的全局，转发鼠标报告用 */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* 2.4G 应用层帧协议（见《2.4G_应用层帧协议设计.md》）：
 *   [0..5] = USB 报告（透传给主机）
 *   [6]    = type(高4位) | seq(低4位)
 *   [7]    = 状态字节（DPI 档位等）
 * seq 去重：ACK 丢失时发端会重发同一包，seq 相同 = 重复，丢弃 */
#define TYPE_DATA  0x0
static uint8_t s_last_seq = 0xFF;   /* 无效初值：上电第一包必收 */

/* ============================================================
 * 应用层实现（dongle 业务）
 *
 * 调用链：
 *   app(本文件) ──▶ link(状态机) ──▶ core(驱动) ──▶ hw(SPI原语)
 *
 * app 只做三件事：
 *   1. 初始化：驱动 → 自检 → 接收模式 → 状态机归零
 *   2. 每圈：收包 → 把"收到包"这个事件喂给状态机
 *   3. 查状态：状态变了才反应（现在打印，以后做灯/USB/重连）
 * ============================================================ */

void NRF24L01_App_Init(void)
{
    printf("\r\n=== 2.4G Dongle App ===\r\n");

    /* ① 驱动初始化：SPI 降速 + CE/CSN 初始电平
     *    （引脚模式是 CubeMX/HAL 配好的，驱动只管电平） */
    NRF24L01_Init();

    /* ② 自检：写 0xA5 再读回，确认模块真的在。
         失败 = 接线/供电/SPI 有问题，直接卡住别往下走，
     */
    if (NRF24L01_Check() == 0) {
        printf("NRF24L01 Check OK\r\n");
    } else {
        printf("NRF24L01 Check FAIL! Halt.\r\n");
        while (1);
    }

    /* ③ 进接收模式：写地址/信道/速率/负载宽度 → 上电 → CE=1 持续监听 */
    NRF24L01_Set_Mode(NRF_MODE_RX);

    /* ④ 状态机归零：从 LINK_DOWN 开始等包 */
    NRF24L01_Link_Init();

    printf("RX Mode Ready, waiting mouse...\r\n");
}

void NRF24L01_App_Task(void) 
{
    uint32_t now = HAL_GetTick();   /* 本圈时间戳，喂给状态机算超时 */
    uint8_t  buf[RX_PLOAD_WIDTH];   /* 收包缓冲：和负载宽度一致（8字节） */
    uint8_t  i;
    /* 记住"上次告诉过应用层的状态"：
     * 状态机可能每圈都是 UP，只有"变了"才打印，避免刷屏 */
    static NRF24L01_LinkState_t s_prev_state = LINK_DOWN;

    /* ── ① 收包：驱动只回答"有没有包"，有就交给链路层 ── */
    if (NRF24L01_Recv(buf) == 0) {
        uint8_t type = buf[6] >> 4;
        uint8_t seq  = buf[6] & 0x0F;

        if (type == TYPE_DATA && seq != s_last_seq) {
            /* 新包：更新去重基准，透传前 6 字节给 USB */
            s_last_seq = seq;
            USBD_HID_SendReport(&hUsbDeviceFS, buf, 6);  /* 枚举前是安全空操作 */

            NRF24L01_Link_On_Packet(now);  /* 喂事件：状态机刷新计时 */

            printf("[%lu] pkt=%lu seq=%u: ", (unsigned long)now,
                   (unsigned long)NRF24L01_Link_Get_Pkt_Cnt(), seq);
            for (i = 0; i < RX_PLOAD_WIDTH; i++) {
                printf("%02X ", buf[i]);   /* 原始数据，好对照发送端 */
            }
            printf("\r\n");
        }
        /* seq == s_last_seq = 重复包（ACK 丢失导致的重发），静默丢弃 */
    }

    /* ── ② 让状态机查超时：每圈都查，与有没有包无关 ── */
    NRF24L01_Link_Poll(now);

    /* ── ③ 状态变化才反应：查询式"边缘检测" ── */
    if (NRF24L01_Link_Get_State() != s_prev_state) {
        s_prev_state = NRF24L01_Link_Get_State();
        printf(s_prev_state == LINK_UP ? "[LINK UP]\r\n" : "[LINK DOWN]\r\n");
    }
}
