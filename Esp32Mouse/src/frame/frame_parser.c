#include "frame_parser.h"

/* ---- 状态机 ---- */
typedef enum
{
    ST_WAIT_AA,   /* 找帧头 AA */
    ST_WAIT_55,   /* 等帧头 55 */
    ST_WAIT_LEN,  /* 等长度 */
    ST_RECV_DATA, /* 收数据 */
    ST_WAIT_SUM,  /* 等校验和 */
} parser_state_t;

static parser_state_t s_state = ST_WAIT_AA; // 状态机
static uint8_t s_buf[FRAME_MAX_LEN];
static uint8_t s_len; // 长度
static uint8_t s_idx; // 索引
static uint8_t s_sum; // 校验和

void FrameParser_Reset(void)
{
    s_state = ST_WAIT_AA;
    s_len = 0;
    s_idx = 0;
    s_sum = 0;
}

int FrameParser_PutByte(uint8_t byte, frame_result_t *result)
{
    while (1)
    {
        switch (s_state)
        {
        case ST_WAIT_AA:
            if (byte == FRAME_HEAD0)
                s_state = ST_WAIT_55;
            return 0;

        case ST_WAIT_55:
            if (byte == FRAME_HEAD1)
            {
                s_state = ST_WAIT_LEN;
                return 0;
            }
            if (byte == FRAME_HEAD0)
                return 0; /* 连续 AA，继续等 55 */
            s_state = ST_WAIT_AA;
            continue; /* 当前字节可能是 AA？重查 */

        case ST_WAIT_LEN:
            if (byte == 0 || byte > FRAME_MAX_LEN)
            {
                s_state = ST_WAIT_AA;
                continue; /* 长度非法，当前字节重查 */
            }
            s_len = byte;
            s_idx = 0;
            s_sum = 0;
            s_state = ST_RECV_DATA;
            return 0;

        case ST_RECV_DATA:
            s_buf[s_idx++] = byte;
            s_sum += byte;
            if (s_idx >= s_len)
                s_state = ST_WAIT_SUM;
            return 0;

        case ST_WAIT_SUM:
            if (byte == s_sum)
            {
                /* 校验通过：输出这一帧 */
                result->len = s_len;
                for (uint8_t i = 0; i < s_len; i++)
                    result->data[i] = s_buf[i];

                s_state = ST_WAIT_AA;
                s_len = 0;
                s_idx = 0;
                s_sum = 0;
                return 1;
            }
            /* 校验失败：丢帧重来，当前字节可能是新帧头 AA */
            s_state = ST_WAIT_AA;
            s_len = 0;
            s_idx = 0;
            s_sum = 0;
            continue;
        }
    }
}
