#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void); // 官方端口函数：推进内核tick、解除到期任务的阻塞

volatile u32 g_ms_tick;
void SystemTick_Init(void)
{
    /* DWT独立计时，避免短微秒延时因任务切换和SysTick回绕而算错。 */
    *(vu32 *)0xE000EDFCUL |= 1UL << 24; // DEMCR.TRCENA：开启跟踪单元访问
    *(vu32 *)0xE0001004UL = 0;          // DWT.CYCCNT：清CPU周期计数
    *(vu32 *)0xE0001000UL |= 1UL;       // DWT.CTRL.CYCCNTENA：开始计数
    SysTick->CTRL =
        (1U << 0) | (1U << 1) | (1U << 2); // 对应使能，计数至0产生中断(置0则为查询),使用系统时钟
    SysTick->LOAD = SystemCoreClock / 1000 - 1; // 1ms,
    SysTick->VAL = 0;                           // 清零
}

void SysTick_Handler(void)
{
    g_ms_tick++; // 1ms 递增
    /* 启动调度器之前还没有就绪任务，不能提前调用内核节拍处理。
     * 调度器暂停调度与“尚未启动”不同：暂停期间内核仍需记录tick。
     */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
}

void Delay_ms(u32 ms)
{
    /* 普通任务上下文才允许调用阻塞API；ISR里绝不能vTaskDelay。
     * 这里能判断ISR，但不会自动发现调用者是否持锁/关闭中断；调用者仍需遵守约束。
     */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && xPortIsInsideInterrupt() == pdFALSE)
    {
        if (ms)
            vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
    /* 调度前的传感器/SROM初始化继续同步等待，不使用RTOS对象。 */
    while (ms--)
        Delay_us(1000);
}

void Delay_us(u32 us)
{
    /* 分段保持差值小于32位回绕周期；抢占只延长最小等待，不重复补等。 */
    while (us)
    {
        u32 slice = us > 1000U ? 1000U : us;
        u32 start = *(vu32 *)0xE0001004UL;
        u32 cycles = slice * (SystemCoreClock / 1000000U); // 72MHz下1us=72周期
        while ((u32)(*(vu32 *)0xE0001004UL - start) < cycles)
        {
            /* pyOCD等调试器断开时可能清DEMCR.TRCENA。
             * 不能只在上电时开一次，否则DWT停数会让这里永久等待，
             * 表现为“接着仿真器能动，退出调试后传感器停止”。
             * 在等待中自恢复：即使断开发生在本次延时中途也能继续。
             * 这是最短等待，抢占和工具停核仍可能把实际等待拉长。 */
            if ((*(vu32 *)0xE000EDFCUL & (1UL << 24)) == 0)
                *(vu32 *)0xE000EDFCUL |= 1UL << 24;
            if ((*(vu32 *)0xE0001000UL & 1UL) == 0)
                *(vu32 *)0xE0001000UL |= 1UL;
        }
        us -= slice;
    }
}
