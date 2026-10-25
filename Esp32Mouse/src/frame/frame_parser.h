#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>

/* 链路帧协议： [AA] [55] [len] [data...] [SUM]
 *   - AA 55       帧头
 *   - len         数据字节数（0~FRAME_MAX_LEN）
 *   - data        数据
 *   - SUM         data 累加和
 */
#define FRAME_HEAD0 0xAA
#define FRAME_HEAD1 0x55
#define FRAME_MAX_LEN 16 /* 单帧数据上限，防恶意长度 */

typedef struct
{
    uint8_t data[FRAME_MAX_LEN];
    uint8_t len;
} frame_result_t;

void FrameParser_Reset(void);

/* 逐字节喂入；凑齐一帧并校验通过返回 1，结果写入 result */
int FrameParser_PutByte(uint8_t byte, frame_result_t *result);

#endif
