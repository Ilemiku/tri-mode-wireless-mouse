#include "si24r1_link.h"

/* ============================================================
 * 状态机三要素：
 *   状态     LINK_DOWN / LINK_UP
 *   事件     发送成功(On_Tx_Result) / 发送失败 / 超时(Poll)
 *   转移表：
 *     DOWN + 发送成功 → UP
 *     UP   + 发送失败 → DOWN
 *     UP   + 超时     → DOWN（安全网，防止"没在发"时状态卡死）
 * ============================================================ */

/* 超过该时间没有发送成功 → 判掉线 */
#define LINK_TIMEOUT_MS 500

/* static：只有本文件能碰，外部只能通过接口读写 —— 封装 */
static SI24R1_LinkState_t s_state = LINK_DOWN;
static u32 s_last_ok_tick = 0; /* 上次发送成功的时刻，超时判定依据 */
static u32 s_pkt_cnt = 0;      /* 发送包数 */
static u32 s_fail_cnt = 0;     /* 发送失败数 */

void SI24R1_Link_Init(void)
{
    s_state = LINK_DOWN; /* 默认：断线 */
    s_last_ok_tick = 0;
    s_pkt_cnt = 0;
    s_fail_cnt = 0;
}

/* 事件：应用层发完一包后调用，ok=1 表示 TX_OK */
void SI24R1_Link_On_Tx_Result(u8 ok, u32 now)
{
    if (ok)
    {
        s_pkt_cnt++;
        s_last_ok_tick = now;
        if (s_state == LINK_DOWN)
        {
            s_state = LINK_UP; /* 转移：DOWN + 成功 → UP */
        }
    }
    else
    {
        s_fail_cnt++;
        if (s_state == LINK_UP)
        {
            s_state = LINK_DOWN; /* 转移：UP + 失败 → DOWN */
        }
    }
}

/* 周期：查超时（安全网） */
void SI24R1_Link_Poll(u32 now)
{
    /* 用 (now - last) 而不是 now > last+500：
     * 32 位计数器会回绕(溢出)，减法式判断天然防回绕，嵌入式标准写法 */
    if (s_state == LINK_UP && (now - s_last_ok_tick > LINK_TIMEOUT_MS))
    {
        s_state = LINK_DOWN;
    }
}

SI24R1_LinkState_t SI24R1_Link_Get_State(void)
{
    return s_state;
}

u32 SI24R1_Link_Get_Pkt_Cnt(void)
{
    return s_pkt_cnt;
}

u32 SI24R1_Link_Get_Fail_Cnt(void)
{
    return s_fail_cnt;
}
