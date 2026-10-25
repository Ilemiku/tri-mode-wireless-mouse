#ifndef APP_RTOS_H
#define APP_RTOS_H
#include "reg32f1.h"

/* 可在 Keil 中观察；统计不是性能测试结论。栈水位单位为32位字。 */
typedef struct
{
    u32 input_cycles;       // 输入任务完成了多少轮采集
    u32 tx_reports;         // 按各通道返回值判断，提交/发送成功多少份
    u32 queue_full;         // 报告队列满，拒绝了多少份新报告
    u32 stale_reports;      // 取出时已超过50ms，作废了多少份
    u32 mode_discards;      // 因模式代次不符而在第一次检查中丢弃的份数
    u32 tx_failed;          // 发送失败次数，包含USB未配置/忙、RF失败
    u32 release_failed;     // 切换模式时，向旧通道提交全释放报告失败次数
    u32 input_overruns;     // 单轮输入执行跨度达到或超过10tick的次数
    u32 rf_timeouts;        // RF等待通知到期且未收到通知的次数
    u32 min_free_heap;      // 自启动以来最少剩余的内核堆，单位字节
    u32 input_stack_free;   // 输入任务的历史最小剩余栈，单位32位字
    u32 tx_stack_free;      // 传输任务的历史最小剩余栈，单位32位字
    u32 service_stack_free; // 服务任务的历史最小剩余栈，单位32位字
    u32 fault_line;         // 断言失败时的行号；钩子故障可能记为0
    const char *fault_file; // 断言文件名或故障类型/任务名称
} App_RtosStats;
extern volatile App_RtosStats g_rtos_stats;
void App_RtosStart(void);
#endif
