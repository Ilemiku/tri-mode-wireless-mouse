#include "nrf24l01_link.h"

/* ============================================================
 * 链路状态机（中间层）：只维护"链路现在什么状态"
 * 不碰硬件，不碰业务。
 *
 * 状态：LINK_DOWN / LINK_UP
 * 事件：收到包(On_Packet) / 超时(Poll 检查)
 * 转移：DOWN --收到包--> UP
 *       UP  --超时-->  DOWN
 * ============================================================ */

/* 超过该时间没收到包 → 判掉线 */
#define LINK_TIMEOUT_MS  500

static NRF24L01_LinkState_t s_state = LINK_DOWN;        // 当前状态
static uint32_t s_last_rx_tick = 0;             // 上一次收到包的时间
static uint32_t s_pkt_cnt = 0;                  // 收包计数

void NRF24L01_Link_Init(void)
{
    s_state = LINK_DOWN;
    s_last_rx_tick = 0;
    s_pkt_cnt = 0;
}

/* 事件：收到一包（应用层 Recv 成功后调用） */
void NRF24L01_Link_On_Packet(uint32_t now)
{
    s_pkt_cnt++;
    s_last_rx_tick = now;

    if (s_state == LINK_DOWN)
    {
        s_state = LINK_UP;          /* 转移：DOWN --收到包--> UP */
    }
}

/* 周期：每圈调用，检查是否超时 */
void NRF24L01_Link_Poll(uint32_t now)
{
    if (s_state == LINK_UP &&
        (now - s_last_rx_tick > LINK_TIMEOUT_MS))
    {
        s_state = LINK_DOWN;        /* 转移：UP --超时--> DOWN */
    }
}

NRF24L01_LinkState_t NRF24L01_Link_Get_State(void)
{
    return s_state; 
}

uint32_t NRF24L01_Link_Get_Pkt_Cnt(void)
{
    return s_pkt_cnt;
}
