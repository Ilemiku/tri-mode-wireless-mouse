/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins. (寄存器版 — 直接操作 RCC 寄存器)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * @brief  GPIO 初始化 — 寄存器版
  *
  *  当前只需要开启 GPIOA 的时钟。
  *  PA11(USB_DM) 和 PA12(USB_DP) 的引脚模式由 USB 外设内部接管，
  *  不需要在 GPIO 层手动配置 CRH。
  *
  *  如果你后续要用其他 GPIO 引脚（比如 LED 指示灯），在这里加配置。
  *
  *  如何配置一个引脚（示例）：
  *    假设 PA8 要配成 推挽输出 50MHz:
  *      GPIOA->CRH &= ~(0xFU << 0);                        // 清 PA8
  *      GPIOA->CRH |=  (GPIO_CFG_OUT_PP_50M << 0);         // 设 PA8
  *    CRH 控制 PIN8~PIN15，每 4bit 一组:
  *      PIN8  → shift=0,   PIN9  → shift=4
  *      PIN10 → shift=8,   PIN11 → shift=12
  *      PIN12 → shift=16,  PIN13 → shift=20
  *      PIN14 → shift=24,  PIN15 → shift=28
  */
void GPIO_Init_Reg(void)
{
    /* ──── 使能 GPIOA 时钟 ──── */
    /*
     * RCC->APB2ENR bit2 (IOPAEN):
     *   写 1 使能 GPIOA 的时钟。在操作 GPIOA 的任何寄存器之前，
     *   必须先把时钟打开，否则写操作不生效甚至 HardFault。
     *
     *   HAL 原写法: __HAL_RCC_GPIOA_CLK_ENABLE();
     *   寄存器等价: RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
     *   使用 reg32f1 宏: CLK_GPIOA_ENABLE();
     */
    CLK_GPIOA_ENABLE();
}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
