#include "Led.h"
#include "systick.h"
#include "reg32f1.h"
#include "gpio.h"
#include "key.h"
#include "uart.h"
#include "pwm3360.h"
#include "usb_hw.h"
#include "usb_core.h"
#include <stdio.h>
#include "mouse.h"
#include "si24r1_app.h"
#include "app_rtos.h"

void SystemClockConfig(void);

int main(void)
{
    SystemClockConfig();

    SystemTick_Init();

    GPIO_Init();

    UART1_Init(); // GPIO表已配置串口引脚；初始化后可以输出启动日志

    UART3_Init();

    Led_Init();

    Key_Init();

    /* PWM3360 初始化：失败自动重试（上电时序/SROM 偶发失败时自愈） */
    for (u8 attempt = 0; attempt < 3; attempt++)
    {
        if (PWM3360_Init() == 0)
            break;
        printf("PWM3360 init failed, retry %d/3...\r\n", attempt + 1);
        Delay_ms(200);
    }

    Mouse_Init(); // 应用层状态：默认 DPI + 同步硬件

    USB_HW_Init(); // USB 硬件初始化

    SI24R1_App_Init(); // 2.4G 发送端初始化

    /* 到这里之前都是传统同步初始化，不存在三个业务任务轮流运行。
         * 此函数创建队列/任务后启动官方调度器，正常情况下不会回到main。
         */
    App_RtosStart();
    for (;;)
    {
    } // 防止意外返回；正常运行时不会走这里
}

void SystemClockConfig(void)
{
    // 配置flash的配置周期
    FLASH_ACR |= FLASH_ACR_CFG;
    // 配置HSE时钟源
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
        ;

    // 配置CFGR（分频，PLL倍数，PLL源)
    RCC->CFGR = RCC_CFGR_72MHZ_CONFIG;
    // 开启PLL
    RCC->CR |= RCC_CR_PLLON;

    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    // 选择PLL作为系统时钟源
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL)
        ; // 等待PLL作为系统时钟源稳定
}
