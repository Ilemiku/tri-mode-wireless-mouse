/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_hal_conf.h
  * @brief   HAL configuration file. (寄存器工程版 — 只为 USB 保留最小 HAL)
  *
  *   说明:
  *   本项目的用户代码 (main, gpio, systick, 中断) 已全部改为纯寄存器操作。
  *   HAL 仅保留以下模块以支持 USB 协议栈：
  *     - HAL (核心): HAL_Delay / HAL_IncTick / HAL_GetTick
  *     - HAL_PCD: USB 外设驱动
  *     - HAL_CORTEX: NVIC 优先级设置 (USB 栈内部使用)
  *
  *   所有与寄存器代码重叠的 HAL 模块已禁用：
  *     GPIO, RCC, FLASH, PWR, DMA, EXTI — 这些全部用 reg32f1.h 寄存器宏替代
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32F1xx_HAL_CONF_H
#define __STM32F1xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* ########################## Module Selection ############################## */
/**
  * @brief 只为 USB 协议栈保留的 HAL 模块
  *
  *   ✅ HAL_MODULE_ENABLED     — 核心: HAL_Delay / HAL_IncTick (USB 延时依赖)
  *   ✅ HAL_PCD_MODULE_ENABLED — USB 外设: 端点管理 / 缓冲区 / 控制传输
  *   ✅ HAL_CORTEX_MODULE_ENABLED — NVIC: USB 栈用 HAL_NVIC_SetPriority
  *
  *   ❌ 以下模块已用寄存器替代，不再启用:
  *     HAL_GPIO_MODULE_ENABLED   → CLK_GPIOx_ENABLE() + GPIOx->CRL/CRH/ODR
  *     HAL_RCC_MODULE_ENABLED    → RCC->CR/CFGR 直接操作 + CLK_xxx_ENABLE() 宏
  *     HAL_FLASH_MODULE_ENABLED  → FLASH->ACR 直接操作
  *     HAL_PWR_MODULE_ENABLED    → CLK_PWR_ENABLE() + RCC->APB1ENR
  *     HAL_DMA_MODULE_ENABLED    → 项目未使用 DMA
  *     HAL_EXTI_MODULE_ENABLED   → 项目未使用外部中断
  */

#define HAL_MODULE_ENABLED              /* 核心 HAL (HAL_Delay/IncTick/GetTick) */
#define HAL_PCD_MODULE_ENABLED          /* USB 外设驱动                        */
#define HAL_CORTEX_MODULE_ENABLED       /* NVIC 函数 (USB 栈内部使用)           */

/*
 * 以下全部禁用 — 用户代码已用寄存器替代:
 */
/*#define HAL_ADC_MODULE_ENABLED   */
/*#define HAL_CAN_MODULE_ENABLED   */
/*#define HAL_CAN_LEGACY_MODULE_ENABLED   */
/*#define HAL_CEC_MODULE_ENABLED   */
/*#define HAL_CRC_MODULE_ENABLED   */
/*#define HAL_DAC_MODULE_ENABLED   */
/*#define HAL_DMA_MODULE_ENABLED   */
/*#define HAL_ETH_MODULE_ENABLED   */
/*#define HAL_EXTI_MODULE_ENABLED  */
/*#define HAL_FLASH_MODULE_ENABLED */
/*#define HAL_GPIO_MODULE_ENABLED  */
/*#define HAL_HCD_MODULE_ENABLED   */
/*#define HAL_I2C_MODULE_ENABLED   */
/*#define HAL_I2S_MODULE_ENABLED   */
/*#define HAL_IRDA_MODULE_ENABLED  */
/*#define HAL_IWDG_MODULE_ENABLED  */
/*#define HAL_MMC_MODULE_ENABLED   */
/*#define HAL_NAND_MODULE_ENABLED  */
/*#define HAL_NOR_MODULE_ENABLED   */
/*#define HAL_PCCARD_MODULE_ENABLED */
/*#define HAL_PWR_MODULE_ENABLED   */
/*#define HAL_RCC_MODULE_ENABLED   */
/*#define HAL_RTC_MODULE_ENABLED   */
/*#define HAL_SD_MODULE_ENABLED    */
/*#define HAL_SDRAM_MODULE_ENABLED */
/*#define HAL_SMARTCARD_MODULE_ENABLED */
/*#define HAL_SPI_MODULE_ENABLED   */
/*#define HAL_SRAM_MODULE_ENABLED  */
/*#define HAL_TIM_MODULE_ENABLED   */
/*#define HAL_UART_MODULE_ENABLED  */
/*#define HAL_USART_MODULE_ENABLED */
/*#define HAL_WWDG_MODULE_ENABLED  */

/* ########################## Oscillator Values adaptation ####################*/
/**
  * @brief 时钟源频率值
  *   注意: 用户代码不再使用 HAL_RCC，这些值仅供 USB 栈的 HAL_PCD 内部使用。
  *   SystemClock_Config_Reg() 中直接写 RCC 寄存器，不读取这些宏。
  */
#if !defined  (HSE_VALUE)
  #define HSE_VALUE    8000000U /*!< 外部 8MHz 晶振 (与 reg32f1.h 中 CFGR 配置一致) */
#endif /* HSE_VALUE */

#if !defined  (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif /* HSE_STARTUP_TIMEOUT */

#if !defined  (HSI_VALUE)
  #define HSI_VALUE    8000000U /*!< 内部 8MHz RC 振荡器 */
#endif /* HSI_VALUE */

#if !defined  (LSI_VALUE)
 #define LSI_VALUE               40000U
#endif /* LSI_VALUE */

#if !defined  (LSE_VALUE)
  #define LSE_VALUE    32768U
#endif /* LSE_VALUE */

#if !defined  (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    5000U
#endif /* LSE_STARTUP_TIMEOUT */

/* ########################### System Configuration ######################### */
/**
  * @brief 系统级 HAL 配置 (USB 栈使用)
  */
#define  VDD_VALUE                    3300U /*!< VDD = 3.3V */
#define  TICK_INT_PRIORITY            15U   /*!< SysTick 优先级最低 (但本项目用 SysTick_Init 自己配) */
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE              1U

/*
 * USB PCD 回调开关 — 保持默认 0 (不使用注册回调，用弱函数覆盖)
 */
#define  USE_HAL_PCD_REGISTER_CALLBACKS         0U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT    1U */

/* ################## Ethernet peripheral configuration ##################### */
/* (本项目不使用 Ethernet，保留占位) */

/* Includes ------------------------------------------------------------------*/
/**
  * @brief 只包含 USB 栈需要的 HAL 模块头文件
  *   注意: HAL_GPIO / HAL_RCC / HAL_FLASH 等已删除，
  *   这些外设用 reg32f1.h 中的寄存器宏操作。
  */

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f1xx_hal_cortex.h"
#endif /* HAL_CORTEX_MODULE_ENABLED */

#ifdef HAL_PCD_MODULE_ENABLED
#include "stm32f1xx_hal_pcd.h"
#endif /* HAL_PCD_MODULE_ENABLED */

/* Exported macro ------------------------------------------------------------*/
#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t* file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_CONF_H */
