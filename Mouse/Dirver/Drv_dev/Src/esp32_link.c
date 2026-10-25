#include "esp32_link.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "mouse.h"
#include "systick.h"
#include <string.h>

/* ZE设置入口与UART管理协议。
 * Input拥有传感器；UI/网络只投递请求；Transfer统一发送结果。
 * 阅读顺序：Init -> Request/LocalRequest -> InputPoll -> TxPoll。
 * 本地快照与远端回执都来自寄存器读回，不靠界面选中值推断成功。
 */
typedef struct
{
    u32 id;   /* 请求序号；本地来源和网络来源分别关联 */
    u16 dpi;  /* set_dpi的目标值 */
    u8 op;    /* 1=读状态，2=设置DPI */
    u8 local; /* 1=触摸界面，0=网络或周期查询 */
} SettingRequest;
static QueueHandle_t s_requests, s_results;
static u8 s_parse_state, s_len, s_index, s_sum, s_payload[16];
static u32 s_last_byte;
static MouseSettingsState s_settings_state;

static void send_payload(const u8 *data, u8 len)
{
    u8 frame[20], i, sum = 0;
    frame[0] = 0xAA;
    frame[1] = 0x55;
    frame[2] = len;
    for (i = 0; i < len; ++i)
    {
        frame[i + 3] = data[i];
        sum = (u8)(sum + data[i]);
    }
    frame[len + 3] = sum;
    UART3_SendBuf(frame, (u16)(len + 4));
}

void Mouse_SettingsInit(void)
{
    s_requests = xQueueCreate(4, sizeof(SettingRequest));
    s_results = xQueueCreate(8, 12);
    configASSERT(s_requests && s_results);
    UART3_EnableRx();
}

u8 Mouse_SettingsRequest(u32 id, u8 operation, u16 dpi)
{
    SettingRequest request;
    request.id = id;
    request.op = operation;
    request.dpi = dpi;
    request.local = 0;
    return s_requests && xQueueSend(s_requests, &request, 0) == pdPASS;
}

u8 Mouse_SettingsLocalRequest(u32 id, u8 operation, u16 dpi)
{
    SettingRequest request;
    request.id = id;
    request.op = operation;
    request.dpi = dpi;
    request.local = 1;
    return s_requests && xQueueSend(s_requests, &request, 0) == pdPASS;
}

void Mouse_SettingsGetState(MouseSettingsState *state)
{
    taskENTER_CRITICAL();
    *state = s_settings_state;
    taskEXIT_CRITICAL();
}

void Mouse_SettingsInputPoll(void)
{
    u8 byte, budget = 96, raw, status, result[12];
    SettingRequest request;
    static u32 last_state;
    static u8 cached[12], have_cached;
    static SettingRequest previous;
    while (budget-- && UART3_TryRead(&byte))
    {
        if ((u32)(g_ms_tick - s_last_byte) > 100)
            s_parse_state = 0;
        s_last_byte = g_ms_tick;
        switch (s_parse_state)
        {
        case 0:
            if (byte == 0xAA)
                s_parse_state = 1;
            break;
        case 1:
            s_parse_state = byte == 0x55 ? 2 : (byte == 0xAA ? 1 : 0);
            break;
        case 2:
            s_len = byte;
            s_index = 0;
            s_sum = 0;
            s_parse_state = byte == 12 ? 3 : 0;
            break;
        case 3:
            s_payload[s_index++] = byte;
            s_sum = (u8)(s_sum + byte);
            if (s_index == s_len)
                s_parse_state = 4;
            break;
        case 4:
            if (byte == s_sum && s_payload[0] == 0xC1 && s_payload[11] == 1)
            {
                u32 id = (u32)s_payload[2] | ((u32)s_payload[3] << 8) | ((u32)s_payload[4] << 16) |
                         ((u32)s_payload[5] << 24);
                (void)Mouse_SettingsRequest(id, s_payload[1],
                                            (u16)(s_payload[6] | ((u16)s_payload[7] << 8)));
            }
            s_parse_state = byte == 0xAA ? 1 : 0;
            break;
        default:
            s_parse_state = 0;
            break;
        }
    }
    /* 只在Input中访问传感器，UI/MQTT都不得直接跨任务写SPI。 */
    if (xQueueReceive(s_requests, &request, 0) == pdPASS)
    {
        if (have_cached && request.id && request.id == previous.id && request.op == previous.op &&
            request.dpi == previous.dpi && request.local == previous.local)
            (void)xQueueSend(s_results, cached, 0);
        else
        {
            memset(result, 0, sizeof(result));
            result[0] = 0xC2;
            result[1] = request.op;
            result[2] = (u8)request.id;
            result[3] = (u8)(request.id >> 8);
            result[4] = (u8)(request.id >> 16);
            result[5] = (u8)(request.id >> 24);
            status = request.op == 1 ? 0 : (request.op == 2 ? Mouse_ApplyDPI(request.dpi) : 1);
            {
                u16 actual = Mouse_ReadActualDPI(&raw);
                result[6] = (u8)actual;
                result[7] = (u8)(actual >> 8);
            }
            result[8] = status;
            result[9] = raw;
            result[10] = (u8)Mouse_ModeGet();
            result[11] = 1;
            /* UI和网络共享Input拥有者，但本地请求ID不冒充网络请求ID。
             * 本地结果通过快照确认；送ESP的对应消息用ID=0作为主动状态。 */
            taskENTER_CRITICAL();
            s_settings_state.updated_ms = g_ms_tick;
            s_settings_state.dpi = (u16)(result[6] | ((u16)result[7] << 8));
            s_settings_state.reg = raw;
            s_settings_state.mode = result[10];
            if (request.local)
            {
                s_settings_state.local_id = request.id;
                s_settings_state.local_status = status;
            }
            taskEXIT_CRITICAL();
            if (request.local)
                result[2] = result[3] = result[4] = result[5] = 0;
            if (request.id)
            {
                memcpy(cached, result, 12);
                previous = request;
                have_cached = 1;
            }
            (void)xQueueSend(s_results, result, 0);
        }
    }
    /* 模式不需读SPI，Input每轮更新；DPI仍以实际寄存器读回为准。 */
    taskENTER_CRITICAL();
    s_settings_state.mode = (u8)Mouse_ModeGet();
    taskEXIT_CRITICAL();
    /* 主动状态快照：本地物理键/UI修改也能反映给网页，所有模式都能管理。 */
    if ((u32)(g_ms_tick - last_state) >= 1000 && uxQueueSpacesAvailable(s_requests))
    {
        last_state = g_ms_tick;
        (void)Mouse_SettingsRequest(0, 1, 0);
    }
}

void Mouse_SettingsTxPoll(void)
{
    u8 result[12];
    if (xQueueReceive(s_results, result, 0) == pdPASS)
        send_payload(result, 12);
}

/* ============================================================
 * 把 6 字节鼠标报告打包成帧发给 ESP32：
 *   [AA] [55] [06] [d0..d5] [SUM]
 *   头   头   长度  报告6字节  校验和
 * AA 55 双头 = 帧开始标记；06 = 数据长度；
 * SUM = d0~d5 累加和，ESP32 解析器算一遍对不上就丢帧。
 * ============================================================ */
void ESP32_Link_Send_Report(const u8 report[6])
{
    u8 frame[10];
    u8 sum = 0;

    frame[0] = 0xAA;
    frame[1] = 0x55;
    frame[2] = 6; /* 数据长度 = 6 字节报告 */

    for (u8 i = 0; i < 6; i++)
    {
        frame[3 + i] = report[i];
        sum += report[i];
    }
    frame[9] = sum; /* 校验和：6 个数据字节累加 */

    UART3_SendBuf(frame, sizeof(frame));
}
