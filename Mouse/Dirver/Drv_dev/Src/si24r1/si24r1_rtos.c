#include "FreeRTOS.h"
#include "task.h"
#include "si24r1.h"
#include "si24r1_rtos.h"
#include "app_rtos.h"
#include "systick.h"

/* 本文件是项目自己写的RTOS适配层，不是官方射频驱动。
 * 下方调用SI24R1核心函数做硬件事务，向上利用FreeRTOS任务通知实现等待。
 * PG6 -> EXTI6，使用EXTI9_5共享中断；此版本只启用其中的第6号线。
 */
#define RF_EXTI_IMR (*(vu32 *)0x40010400UL)
#define RF_EXTI_RTSR (*(vu32 *)0x40010408UL)
#define RF_EXTI_FTSR (*(vu32 *)0x4001040CUL)
#define RF_EXTI_PR (*(vu32 *)0x40010414UL)
#define RF_AFIO_EXTICR2 (*(vu32 *)0x4001000CUL)
#define RF_LINE (1UL << 6)
static TaskHandle_t s_waiter; // 保存“要叫醒哪个任务”；不是消息数据、也不是中断函数

void SI24R1_RtosIrqInit(void)
{
    RCC_APB2ENR_AFIOEN();
    RF_EXTI_IMR &= ~RF_LINE; // 初始化和空闲期间先不让RF线产生CPU中断
    RF_AFIO_EXTICR2 = (RF_AFIO_EXTICR2 & ~(15UL << 8)) | (6UL << 8); // EXTI6选GPIOG，端口编码6
    RF_EXTI_RTSR &= ~RF_LINE;                                        // 不监听上升沿
    RF_EXTI_FTSR |= RF_LINE;   // IRQ高变低时，表示芯片有完成/失败等状态
    RF_EXTI_PR = RF_LINE;      /* 写1清挂起。 */
    NVIC->IPR[23] = 6U << 4;   /* 数值6低于内核API最高允许紧急程度5。 */
    NVIC->ICPR[0] = 1UL << 23; // 清NVIC旧挂起；EXTI9_5的外部中断编号是23
    NVIC->ISER[0] = 1UL << 23; // 允许CPU响应，实际EXTI线在每次发送前才放行
}

void EXTI9_5_IRQHandler(void)
{
    BaseType_t woken = pdFALSE; // 官方整数类型；初始表示还没唤醒更高优先级任务
    if (RF_EXTI_PR & RF_LINE)
    {
        RF_EXTI_PR = RF_LINE;
        /* 官方ISR专用API：给指定任务的通知计数加1。
         * 如唤醒的任务比被中断任务优先级高，内核会通过&woken写回pdTRUE。
         * 这里只发通知，不读SPI、不printf、不调用阻塞API。
         */
        if (s_waiter != NULL)
            vTaskNotifyGiveFromISR(s_waiter, &woken);
        portYIELD_FROM_ISR(woken); // 必要时挂起PendSV，等退出中断后再切任务
    }
}

u8 SI24R1_RtosSend(u8 *packet)
{
    u8 result;
    s_waiter = xTaskGetCurrentTaskHandle(); /* 只有传输任务访问射频芯片。 */
    RF_EXTI_IMR &= ~RF_LINE;
    SI24R1_TxPrepare();                /* CE低、清旧状态/FIFO；清通知必须在本次触发前。 */
    (void)ulTaskNotifyTake(pdTRUE, 0); // 清掉上次遗留通知；0表示不等待
    RF_EXTI_PR = RF_LINE;
    RF_EXTI_IMR |= RF_LINE; // 放行EXTI，必须先准备好通知路径再触发本次发送
    SI24R1_TxStart(packet); // 写TX FIFO并拉高CE，之后射频芯片自己发包和自动重试
    /* 保证触发所需的最小高脉宽，然后进入可调度的阻塞等待。 */
    Delay_us(15);
    /* IRQ已低：完成事件可能比我们进入等待更早，直接去读结果，不盲等新边沿。
     * IRQ仍高：通知计数非0就立即取走；否则本任务阻塞，最多等20tick。
     * pdTRUE表示取走时把通知计数清0；返回0表示没拿到通知，不能说发送一定失败。
     * 例如边沿丢失但硬件已完成，最后仍要读STATUS确认结果。
     * 通知不是ACK本身，它只表示“该检查芯片状态了”。
     */
    if (SI24R1_IRQ != 0 && ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)) == 0)
        g_rtos_stats.rf_timeouts++;
    RF_EXTI_IMR &= ~RF_LINE;    // 本次等待结束，收口后读取状态；不让旧事件继续通知
    result = SI24R1_TxFinish(); /* 即使超时也读状态并清理，防止永远卡住。 */
    return result;
}
