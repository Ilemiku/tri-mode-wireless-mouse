#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
/* Cortex-M3、72MHz、1ms 节拍；初次接入先关闭无节拍低功耗。 */
#define configUSE_PREEMPTION 1              // 高优先级任务一就绪，就可以抢占低优先级任务
#define configUSE_TIME_SLICING 1            // 同一最高就绪优先级有多个任务时，允许tick轮转
#define configCPU_CLOCK_HZ 72000000UL       // 实际CPU时钟，用于端口配置SysTick
#define configTICK_RATE_HZ 1000U            // 每秒1000tick，本工程1tick=1ms
#define configMAX_PRIORITIES 5              // 可用任务优先级0~4；与NVIC优先级不是一套数字
#define configMINIMAL_STACK_SIZE 128U       // 内核空闲任务栈深度，单位字，不是字节
#define configTOTAL_HEAP_SIZE (16U * 1024U) // heap_4使用的16KB字节数组，来自普通SRAM
#define configMAX_TASK_NAME_LEN 12          // 调试任务名缓冲长度，包含字符串结尾
#define configUSE_16_BIT_TICKS 0            // 不用16位tick；当前端口使用32位tick
#define configIDLE_SHOULD_YIELD 1           // 若有同为优先级0的应用任务，空闲任务可让位
#define configUSE_MUTEXES 1                 // 编译互斥量支持；本版业务没有创建互斥量对象
#define configUSE_COUNTING_SEMAPHORES 0     // 本版不用独立计数信号量，关闭它
#define configUSE_TASK_NOTIFICATIONS 1      // RF完成事件使用每任务自带的通知计数
#define configUSE_TIMERS 0                  // 不创建软件定时器服务任务，周期用任务延时实现
#define configUSE_CO_ROUTINES 0             // 不用协程，使用普通任务
#define configSUPPORT_DYNAMIC_ALLOCATION 1  // 允许xTaskCreate/xQueueCreate启动时分配内存
#define configSUPPORT_STATIC_ALLOCATION 0   // 本版未用静态创建API，不等于没有static变量
#define configCHECK_FOR_STACK_OVERFLOW 2    // 在调度检查点检测栈越界迹象，不能保证抓住所有越界
#define configUSE_MALLOC_FAILED_HOOK 1      // 内核分配失败时调用项目提供的故障钩子
#define configUSE_TICK_HOOK 0               // 不在每次tick额外调用应用钩子
#define configUSE_IDLE_HOOK 0               // 不增加应用空闲钩子；默认空闲不保证硬件睡眠
#define configUSE_TICKLESS_IDLE 0           // 初版不暂停tick，不宣称已经实现低功耗
#define configPRIO_BITS 4                   // F103实际实现4个NVIC优先级位
#define configKERNEL_INTERRUPT_PRIORITY (15U << 4) // PendSV/SysTick设为最低中断紧急程度
/* RVDS汇编端口也会展开此宏，使用纯数值而非C语言移位表达式。 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x50 // 逻辑优先级5左移4位；调用内核的ISR数值要>=5
/* INCLUDE开关决定这些可选API是否参与编译；1不是执行一次的意思。 */
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

/* 强符号覆盖启动文件里的同名弱默认异常处理函数。 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
void App_AssertFailed(const char *file, uint32_t line);
#define configASSERT(x)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
            App_AssertFailed(__FILE__, __LINE__);                                                  \
    } while (0)
/* 断言不是自动修复：条件为假时，保存编译器提供的文件名和行号，然后停住。 */
#endif
