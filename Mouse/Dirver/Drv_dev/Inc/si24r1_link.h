#ifndef __SI24R1_LINK_H__
#define __SI24R1_LINK_H__

#include "reg32f1.h"

/* ============================================================
 * TX 链路状态机（中间层）
 *
 * 发送端没有"等包"，靠发送结果判断链路：
 *   LINK_DOWN + 发送成功(TX_OK)  → LINK_UP
 *   LINK_UP   + 发送失败(MAX_TX) → LINK_DOWN
 *   LINK_UP   + 超时没成功       → LINK_DOWN（安全网）
 *
 * 为什么发送成功 = 链路 UP：
 *   开着自动应答时，TX_OK 意味着收到了对面回传的 ACK——
 *   "对面还活着"就是这条链路的意义。
 *
 * 职责：只维护"链路什么状态"，不碰硬件、不碰业务。
 * ============================================================ */

typedef enum
{
    LINK_DOWN = 0,
    LINK_UP
} SI24R1_LinkState_t;

void SI24R1_Link_Init(void);
void SI24R1_Link_On_Tx_Result(u8 ok, u32 now); /* 事件：发完一包 */
void SI24R1_Link_Poll(u32 now);                /* 周期：查超时 */
SI24R1_LinkState_t SI24R1_Link_Get_State(void);
u32 SI24R1_Link_Get_Pkt_Cnt(void);
u32 SI24R1_Link_Get_Fail_Cnt(void);

#endif /* __SI24R1_LINK_H__ */
