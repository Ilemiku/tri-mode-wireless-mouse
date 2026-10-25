#ifndef __NRF24L01_LINK_H__
#define __NRF24L01_LINK_H__

#include <stdint.h>

/* ============================================================
 * 链路状态机（中间层）
 *
 * 职责：只维护"这条 2.4G 链路现在处于什么状态"。
 *       不碰硬件（不调 Recv/Send），不碰业务（不打印/不转 USB）。
 *
 * 给应用层的接口分三类：
 *   初始化     Link_Init()          上电/重连前归零
 *   喂事件     Link_On_Packet()     收到一包时调用（事件驱动）
 *   周期检查   Link_Poll()          主循环每圈调用，查超时（时间驱动）
 *   查询       Link_Get_State()     应用层不猜状态，只问状态机
 *              Link_Get_Pkt_Cnt()   收包计数（统计/显示用）
 * ============================================================ */

typedef enum {
    LINK_DOWN = 0,   /* 无连接：正在等包 */
    LINK_UP          /* 已连接：持续收到包 */
} NRF24L01_LinkState_t;

void                 NRF24L01_Link_Init(void);
void                 NRF24L01_Link_On_Packet(uint32_t now);  /* 事件：收到一包 */
void                 NRF24L01_Link_Poll(uint32_t now);       /* 周期：查超时 */
NRF24L01_LinkState_t NRF24L01_Link_Get_State(void);
uint32_t             NRF24L01_Link_Get_Pkt_Cnt(void);

#endif /* __NRF24L01_LINK_H__ */
