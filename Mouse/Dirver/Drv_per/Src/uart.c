#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"

/* 调度后printf只入字节环形缓冲，由TXE中断搬运；满则丢调试字符并计数。 */
#define LOG_BUFFER_SIZE 2048U
static u8 s_log_buffer[LOG_BUFFER_SIZE];    // 日志字符仓库，不是FreeRTOS消息队列
static volatile u32 s_log_head, s_log_tail; // head是下一写入位置，tail是下一读取位置
static volatile u8 s_log_async;             // 启动前0=轮询，开启后1=中断搬运
volatile u32 g_log_dropped;                 // 满缓冲丢掉的字符数；这不计入鼠标报告丢失

/* USART3只在中断中接收字节，解析和传感器设置仍归Input任务。 */
static volatile u8 s_rx3[128];
static volatile u32 s_rx3_head, s_rx3_tail;
volatile u32 g_uart3_rx_errors;

void UART3_EnableRx(void)
{
    NVIC->IPR[39] = 6U << 4;
    NVIC->ICPR[1] = 1UL << 7;
    NVIC->ISER[1] = 1UL << 7;
    USART3->CR1 |= 1UL << 5;
}

void USART3_IRQHandler(void)
{
    u32 sr = USART3->SR;
    if (sr & 0x2FU) /* RXNE及接收错误：读SR后读DR清状态。 */
    {
        u8 byte = (u8)USART3->DR;
        u32 next = (s_rx3_head + 1U) & 127U;
        if ((sr & 0x0FU) || next == s_rx3_tail)
            g_uart3_rx_errors++;
        else if (sr & (1UL << 5))
        {
            s_rx3[s_rx3_head] = byte;
            s_rx3_head = next;
        }
    }
}

int UART3_TryRead(u8 *byte)
{
    if (s_rx3_tail == s_rx3_head)
        return 0;
    *byte = s_rx3[s_rx3_tail];
    s_rx3_tail = (s_rx3_tail + 1U) & 127U;
    return 1;
}

void UART1_EnableAsync(void)
{
    NVIC->IPR[37] = 6U << 4; // USART1外部中断编号37，能被内核临界区短暂屏蔽
    NVIC->ICPR[1] = 1UL << 5;
    NVIC->ISER[1] = 1UL << 5;
    s_log_async = 1;
}

void USART1_IRQHandler(void)
{
    /* TXE表示发送数据寄存器可写，不表示线上最后一个停止位已经发完。
     * 必须同时检查TXEIE使能位，避免别的状态让我们误进入此分支。
     */
    if ((USART1->SR & USART_SR_TXE) && (USART1->CR1 & (1UL << 7)))
    {
        if (s_log_tail != s_log_head)
        {
            USART1->DR = s_log_buffer[s_log_tail];
            s_log_tail = (s_log_tail + 1U) % LOG_BUFFER_SIZE;
        }
        else
            USART1->CR1 &= ~(1UL << 7); // 没字符就关TXE中断，否则会不断空中断
    }
}

void UART1_Init(void)
{
    RCC_APB2ENR_USART1EN(); // 使能 USART1 时钟
    RCC_APB2ENR_GPIOAEN();
    /*
    GPIOA->CRH &= ~(0xF << (1 * 4)); // 清除 PA9 的配置
    GPIOA->CRH |= GPIO_MODE_OUT_AF_PP_50M << (1 * 4); // 设置 PA9 为复用推挽输出，50MHz

    GPIOA->CRH &= ~(0xF << (2 * 4)); // 清除 PA10 的配置
    GPIOA->CRH |= GPIO_MODE_IN_FLOATING << (2 * 4); // 设置 PA10 为浮空输入
    */
    USART1->BRR = 0x271; // PCLK2=72MHz，BRR=625，对应115200波特率
    Reg_Set(USART1->CR1, USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
}

void UART1_SendByte(u8 ch)
{
    if (s_log_async)
    {
        u32 next;
        /* 防止任务入队与TXE中断出队同时改缓冲状态；仅保护几个指令。
         * 这是普通任务API，调度后UART1_SendByte不能从ISR调用。
         * 此保护只保证单字符入队安全，不保证多个任务的整行printf不交错。
         * 当前运行期printf集中在ServiceTask，启动日志在调度前输出。
         */
        taskENTER_CRITICAL();
        next = (s_log_head + 1U) % LOG_BUFFER_SIZE;
        if (next == s_log_tail)
            g_log_dropped++;
        else
        {
            s_log_buffer[s_log_head] = ch;
            s_log_head = next;
            USART1->CR1 |= 1UL << 7; // 唤起硬件搬运，函数不用等这个字符发完
        }
        taskEXIT_CRITICAL();
        return;
    }
    while (!(USART1->SR & USART_SR_TXE))
        ;
    USART1->DR = ch;
}

void UART3_Init(void)
{
    RCC_APB1ENR_USART3EN(); // 使能 USART3 时钟
    RCC_APB2ENR_GPIOBEN();
    /*
    GPIOB->CRH &= ~(0xF << (2 * 4)); // 清除 PB10 的配置
    GPIOB->CRH |= GPIO_MODE_OUT_AF_PP_50M << (2 * 4); // 配置 PB10 为复用推挽

    GPIOB->CRH &= ~(0xF << (3 * 4));       // (11-8)=3
    GPIOB->CRH |= GPIO_MODE_IN_FLOATING << (3 * 4);
    */
    USART3->BRR = 312; // 36MHz/115200 ≈ 312
    Reg_Set(USART3->CR1, USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
}

void UART3_SendByte(u8 ch)
{
    /* BLE桥接当前仍轮询TXE，和UART1异步日志不同；高优先级Input仍可抢占它。 */
    while (!(USART3->SR & USART_SR_TXE))
        ;
    USART3->DR = ch;
}

void UART3_SendBuf(const u8 *buf, u16 len)
{
    for (u16 i = 0; i < len; i++)
    {
        UART3_SendByte(buf[i]);
    }
}

int fputc(int ch, FILE *f)
{
    (void)f;
    UART1_SendByte((u8)ch);
    return ch;
}
