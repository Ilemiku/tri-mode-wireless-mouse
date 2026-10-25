#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "app_rtos.h"
#include "mouse.h"
#include "key.h"
#include "usb_core.h"
#include "usb_hw.h"
#include "esp32_link.h"
#include "si24r1_app.h"
#include "si24r1_rtos.h"
#include "si24r1_link.h"
#include "uart.h"
#include "systick.h"
#include "app_ui.h"
#include "lcd_bus.h"
#include "touch_gt9xx.h"
#include <stdio.h>

/* 本文件是项目自己的任务安排，不是FreeRTOS官方内核。
 * 官方负责：让任务睡下/醒来、管理队列、切换CPU。
 * 本文件负责：哪个任务读传感器、哪个任务发送、消息装什么、失败怎么办。
 * 阅读顺序建议：先看底部App_RtosStart，再看InputTask、TransferTask、ServiceTask。
 */
typedef struct
{
    u8 report[6];    // 真正交给主机的6字节；里面没有RTOS句柄
    u8 mode;         // 采集这份报告时的模式：0=USB，1=2.4G，2=BLE
    u8 dpi;          // 当时的DPI档位，避免发送时读到后来已改变的值
    u32 generation;  // 模式切换代次；切出去又切回来，也算不同会话
    TickType_t time; // 入队前的内核时间，单位tick；本工程1tick=1ms
} ReportMessage;
volatile App_RtosStats g_rtos_stats;          // 只供调试观察；volatile不等于互斥锁
static QueueHandle_t s_reports;               // 队列句柄：找到队列对象的“把手”，不是报告本身
static TaskHandle_t s_input, s_tx, s_service; // 任务句柄，用于读取各自栈余量
static volatile u32 s_generation;             // 仅输入任务修改，传输任务读取

/* 输入任务：高优先级、每10ms一轮。它只采集和投递，不等无线ACK。
 * arg是xTaskCreate传来的任务参数，此处传NULL，因此用(void)arg消除未使用提示。
 * 任务函数必须长期运行，不能执行return返回到调用者。
 */
static void InputTask(void *arg)
{
    TickType_t wake = xTaskGetTickCount(); // 以任务开始时刻作为周期基准
    mouse_mode_t previous = Mouse_ModeGet();
    ReportMessage message; // 存在输入任务自己的栈中，入队时内核复制整个结构体
    (void)arg;
    for (;;)
    {
        TickType_t start = xTaskGetTickCount(); // 本轮真正开始时刻，用于粗略超期统计
        Key_Scan();                             // 内部消抖；可能更新按键位图、DPI、当前模式
        Mouse_SettingsInputPoll();              // 执行UI/远程设置，与传感器采集共用同一拥有者
        if (Mouse_ModeGet() != previous)
        {
            previous = Mouse_ModeGet();
            s_generation++; /* 标记切换前的排队报告，避免旧运动串入新会话。 */
        }
        Mouse_Report(message.report); // SPI读PWM3360，然后构造6字节报告
        message.mode = (u8)previous;
        message.dpi = Mouse_GetDPILevel();
        message.generation = s_generation;
        message.time = xTaskGetTickCount();
        /* 参数1：往哪个队列放；参数2：从哪个地址复制；参数3：最多等多久。
         * 这里0表示队列满立即失败，绝不让采集任务排队等空位。
         * 成功后队列已有副本，下一轮覆盖message不会破坏已排队的内容。
         */
        if (xQueueSend(s_reports, &message, 0) != pdPASS)
            g_rtos_stats.queue_full++; /* 首版明确计数；不能宣称满队列不丢位移。 */
        g_rtos_stats.input_cycles++;
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(10)) // 不是精确抖动测量
            g_rtos_stats.input_overruns++;
        /* 官方API：基准wake每次前进10tick，只阻塞本任务，CPU让给别人。
         * &wake让内核更新该变量；pdMS_TO_TICKS把毫秒换算成tick。
         * 如果目标时刻已经过去，本次可能不睡；因此不能靠它保证硬性准时。
         */
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

/* 普通函数，不是第四个任务！由TransferTask调用，所以用的仍是传输任务的栈。
 * 返回1在各通道含义不同：USB已提交、RF收到ACK、BLE的UART帧已写出。
 * 它们都不能等同于“Windows已经处理了移动”。
 */
static u8 SendReport(u8 mode, const u8 *report, u8 dpi)
{
    if (mode == MOUSE_MODE_USB)
        return usb_config_value == 1 && USB_HW_EpTryTx(1, report, 6); // &&短路：未配置不碰EP1
    if (mode == MOUSE_MODE_2_4G)
        return SI24R1_App_Transmit(report, dpi);
    if (mode == MOUSE_MODE_BLE)
    {
        ESP32_Link_Send_Report(report); /* 首版保留有限的UART轮询发送，约0.87ms。 */
        return 1;
    }
    return 0;
}

/* 传输任务：CPU调度上低于输入任务；三种出口共用它，没有三个发送任务互抢。
 * 同一时刻只处理一份消息。RF等待会阻塞，USB提交很短，UART3当前仍忙等。
 */
static void TransferTask(void *arg)
{
    ReportMessage message;
    u8 previous_mode = MOUSE_MODE_USB;
    u8 zero[6] = {0}; // 所有按键释放，位移/滚轮也为0
    (void)arg;
    for (;;)
    {
        /* 链路状态只由此任务写，后台任务仅取快照并打印。 */
        SI24R1_Link_Poll(g_ms_tick);
        Mouse_SettingsTxPoll(); // UART所有发送都由此任务完成，避免帧字节交错
        /* 队列非空：取出最早的消息并复制到自己的message中，同时队列少一项。
         * 队列为空：最多阻塞10tick；收到消息或到期后才再次有资格运行。
         * 超时没有消息就continue回循环，顺便继续维护链路超时状态。
         */
        if (xQueueReceive(s_reports, &message, pdMS_TO_TICKS(10)) != pdPASS)
            continue;
        if (message.generation != s_generation)
        {
            g_rtos_stats.mode_discards++;
            continue;
        }
        if (message.mode != previous_mode)
        {
            /* 尝试给旧主机释放按键；失联时不能保证送达，也不能永久等。 */
            if (!SendReport(previous_mode, zero, message.dpi))
                g_rtos_stats.release_failed++;
            previous_mode = message.mode;
        }
        /* 给旧通道发释放报告期间，输入任务仍可能切模式，因此必须再检查一次。 */
        if (message.generation != s_generation)
            continue;
        if (xTaskGetTickCount() - message.time > pdMS_TO_TICKS(50))
        {
            g_rtos_stats.stale_reports++;
            continue; /* 不在断链恢复后回放过期运动。 */
        }
        /* 这里才做三选一。执行途中Input醒来可以抢占CPU，但不会撤销硬件传输。
         * 本版不是完整的切换握手：检查后仍可能发生切换，已有在途包可以完成。
         */
        if (SendReport(message.mode, message.report, message.dpi))
            g_rtos_stats.tx_reports++;
        else
            g_rtos_stats.tx_failed++;
    }
}

/* 后台服务任务：低优先级，负责日志，不抢占采集和传输。
 * 每10ms检查一次，但RF统计约每1秒、RTOS统计每2秒才打印。
 */
static void ServiceTask(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    u32 last_stats = g_ms_tick;
    u8 previous_dpi = Mouse_GetDPILevel();
    (void)arg;
    for (;;)
    {
        USB_EventPrint(); /* 调试字符串只从低优先级任务输出。 */
        SI24R1_App_Task();
        if (Mouse_GetDPILevel() != previous_dpi)
        {
            static const u16 dpi_values[] = {800, 1600, 3200, 6400};
            previous_dpi = Mouse_GetDPILevel();
            printf("DPI=%u\r\n", (unsigned int)dpi_values[previous_dpi]);
        }
        if ((u32)(g_ms_tick - last_stats) >= 2000U)
        {
            last_stats = g_ms_tick;
            /* 历史最小剩余堆和最小栈余量，用于判断预算够不够。
             * 栈水位是按填充值估算的余量，不是任务实时CPU占用。
             */
            g_rtos_stats.min_free_heap = xPortGetMinimumEverFreeHeapSize();
            g_rtos_stats.input_stack_free = uxTaskGetStackHighWaterMark(s_input);
            g_rtos_stats.tx_stack_free = uxTaskGetStackHighWaterMark(s_tx);
            g_rtos_stats.service_stack_free = uxTaskGetStackHighWaterMark(s_service);
            printf("RTOS: in=%lu tx=%lu full=%lu stale=%lu fail=%lu heap=%lu\r\n",
                   (unsigned long)g_rtos_stats.input_cycles, (unsigned long)g_rtos_stats.tx_reports,
                   (unsigned long)g_rtos_stats.queue_full,
                   (unsigned long)g_rtos_stats.stale_reports, (unsigned long)g_rtos_stats.tx_failed,
                   (unsigned long)g_rtos_stats.min_free_heap);
            printf("UI: stage=%u lcd=%04x touch=%s dma=%lu err=%lu/%lu stack=%lu\r\n",
                   (unsigned)g_ui_stats.stage, (unsigned)g_ui_stats.lcd_id,
                   g_ui_stats.touch_backend == 2 ? "XPT-ADC" : g_touch_product,
                   (unsigned long)g_lcd_dma_done, (unsigned long)g_ui_stats.dma_timeouts,
                   (unsigned long)g_lcd_dma_errors, (unsigned long)g_ui_stats.stack_free);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

/* main只调用一次：先准备对象，再创建任务，最后交出CPU控制权给调度器。
 * xTaskCreate只是登记任务，调度器尚未启动时不会立即执行任务函数。
 */
void App_RtosStart(void)
{
    BaseType_t created;
    /* VTOR指向Flash向量表；4位全部用于抢占优先级。 */
    *(vu32 *)0xE000ED08UL = 0x08000000UL; // VTOR：异常向量表放在本工程Flash起点
    *(vu32 *)0xE000ED0CUL = 0x05FA0300UL; // AIRCR：写入钥匙05FA，分组值3对应4位抢占
    NVIC->IPR[20] = 4U << 4;              /* USB中断不调用RTOS接口，高于内核屏蔽阈值。 */
    SI24R1_RtosIrqInit();
    Mouse_SettingsInit();
    s_reports = xQueueCreate(16, sizeof(ReportMessage)); // 16份消息，不是16字节
    configASSERT(s_reports != NULL); // 分配失败就记录并停住，不能用空句柄继续运行
    /* 参数：入口函数、调试名称、栈深度、入口参数、任务优先级、返回句柄的地址。
     * 此Cortex-M3端口1个栈单位=4字节，所以384字=1536字节，不是384字节。
     */
    created = xTaskCreate(InputTask, "Input", 384, NULL, 3, &s_input);
    configASSERT(created == pdPASS);
    created = xTaskCreate(TransferTask, "Transfer", 384, NULL, 2, &s_tx);
    configASSERT(created == pdPASS);
    created = xTaskCreate(ServiceTask, "Service", 768, NULL, 1, &s_service);
    configASSERT(created == pdPASS);
    App_UI_Start(); /* 独立低优先级UI，不把重绘塞进Input或Transfer。 */
    printf("FreeRTOS " tskKERNEL_VERSION_NUMBER " ready: Input/Transfer/Service/UI\r\n");
    UART1_EnableAsync();              // 调度后的日志改成缓冲+中断搬运，避免等每个字节发完
    vTaskStartScheduler();            // 官方入口：创建空闲任务、配置节拍、切入首个任务
    App_AssertFailed("scheduler", 0); // 正常不会返回；常见返回原因是内核堆不足
}

/* 断言/堆不足/栈溢出统一落到这里。不要在这里printf：打印路径也可能已经损坏。 */
void App_AssertFailed(const char *file, u32 line)
{
    g_rtos_stats.fault_file = file;
    g_rtos_stats.fault_line = line;
    portDISABLE_INTERRUPTS(); // 本端口提升BASEPRI，并非屏蔽所有更高紧急程度的中断
    for (;;)
    {
    } /* 不依赖printf；在调试器查看故障文件和行号。 */
}
/* 以下函数名由官方约定；实现由项目提供，内核检测异常时会回调我们。 */
void vApplicationMallocFailedHook(void)
{
    App_AssertFailed("heap", 0);
}
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    App_AssertFailed(name, 0);
}
