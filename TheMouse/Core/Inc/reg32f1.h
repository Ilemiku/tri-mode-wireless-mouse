/**
  ******************************************************************************
  * @file    reg32f1.h
  * @brief   STM32F103 寄存器操作框架
  *
  *  说明：
  *  - CMSIS 已经提供了外设地址映射 (RCC_BASE)、结构体 (RCC_TypeDef)、
  *    外设指针 (RCC)、位定义 (RCC_CR_HSION) 等，勿重复定义。
  *  - 本文件补充：便捷宏、字段组合值、GPIO 配置常量、外设时钟快捷宏。
  ******************************************************************************
  */

#ifndef __REG32F1_H__
#define __REG32F1_H__

#include <stdint.h>
#include <stm32f1xx.h>
//位操作
/*=====================================================================================*/

//对reg的指定bits置位
#define REG_SET(reg, bits)    ((reg) |= (uint32_t)(bits)) // 设置

//对reg的指定bits清零
#define REG_CLR(reg, bits)    ((reg) &= ~(uint32_t)(bits)) // 清零

//清除reg中clr位域，填入set位域，完成读改写
#define REG_WR(reg,clr,set)   ((reg)=(((reg) &~(uint32_t)(clr))|(uint32_t)(set)))

//读取reg的bits位域
#define REG_RD(reg, bits)     ((reg)&(uint32_t)(bits))

/*=====================================================================================*/

//外设时钟使能宏
/*=====================================================================================*/

/* ---- AHB 总线 ---- */
#define CLK_DMA1_ENABLE()    (RCC-> AHBENR |= RCC_AHBENR_DMA1EN) //bit0,DMA1使能

/* ---- APB2 总线 (最高 72MHz) ---- */
#define CLK_AFIO_ENABLE()    (RCC->APB2ENR |=RCC_APB2ENR_AFIOEN) 
//bit0,AFIO使能 (RCC_APB2ENR_AFIOEN=0x00000001)
#define CLK_GPIOA_ENABLE()   (RCC->APB2ENR |=RCC_APB2ENR_IOPAEN)
 //bit2,GPIOA使能 (RCC_APB2ENR_IOPAEN=0x00000004)
#define CLK_GPIOB_ENABLE()   (RCC->APB2ENR |=RCC_APB2ENR_IOPBEN) 
//bit3,GPIOB使能 (RCC_APB2ENR_IOPBEN=0x00000008)
#define CLK_GPIOC_ENABLE()   (RCC->APB2ENR |=RCC_APB2ENR_IOPCEN) 
//bit4,GPIOC使能 (RCC_APB2ENR_IOPCEN=0x00000010)
#define CLK_GPIOD_ENABLE()   (RCC->APB2ENR |=RCC_APB2ENR_IOPDEN)
 //bit5,GPIOD使能 (RCC_APB2ENR_IOPDEN=0x00000020)
#define CLK_GPIOE_ENABLE()   (RCC->APB2ENR |= RCC_APB2ENR_IOPEEN) 
//bit6,GPIOE使能 (RCC_APB2ENR_IOPEEN=0x00000040)
#define CLK_GPIOF_ENABLE()   (RCC->APB2ENR |=RCC_APB2ENR_IOPFEN) 
//bit7,GPIOF使能 (RCC_APB2ENR_IOPFEN=0x00000080)
#define CLK_GPIOG_ENABLE()   (RCC->APB2ENR |=RCC_APB2ENR_IOPGEN) 
//bit8,GPIOG使能 (RCC_APB2ENR_IOPGEN=0x00000100)
#define CLK_ADC1_ENABLE()    (RCC->APB2ENR |=RCC_APB2ENR_ADC1EN)
 //bit9,ADC1使能 （RCC_APB2ENR_ADC1En=0x00000200)
#define CLK_ADC2_ENABLE()    (RCC->APB2ENR |=RCC_APB2ENR_ADC2EN) 
//bit10,ADC2使能 （RCC_APB2ENR_ADC2En=0x00000400)
#define CLK_USART1_ENABLE()  (RCC->APB2ENR |=RCC_APB2ENR_USART1EN) 
//bit14,USART1使能 (RCC_APB2ENR_USART1EN=0x00004000)

/* ---- APB1 总线 (最高 36MHz) ---- */
#define CLK_USB_ENABLE()     (RCC->APB1ENR |=RCC_APB1ENR_USBEN)  
//bit23,USB使能 (RCC_APB1ENR_USBEN=0x00800000)
#define CLK_PWR_ENABLE()     (RCC->APB1ENR |=RCC_APB1ENR_PWREN)  
//bit28,PWR使能 (RCC_APB1ENR_PWREN=0x10000000)

/* ================================================================== */
/*  3. Flash 控制器 (FLASH->ACR)                                       */
/* ================================================================== */

#define FLASH_ACR_LATENCY_0   (0U << 0)
#define FLASH_ACR_LATENCY_1   (1U << 0)
#define FLASH_ACR_LATENCY_2   (2U << 0)
#define FLASH_ACR_PRFTBE      (1u << 4)

/*
 * 一步配置 Flash：设等待周期 + 开预取
 * 用法：FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;
 */

/* ================================================================== */
/*  4. RCC CFGR 寄存器的字段值                                         */
/*     这些值可以直接 OR 在一起写入 RCC->CFGR                          */
/* ================================================================== */

/* ---- 4.1 系统时钟源选择 SW[1:0] ---- */
#define CFGR_SW_HSE (1U << 0)  // 系统时钟源为 HSE
#define CFGR_SW_PLL (2U << 0)  // 系统时钟源为 PLL
#define CFGR_SW_HSI (0U << 0)  // 系统时钟源为 HSI
#define CFGR_SW_Msk (3U << 0)
/* ---- 4.2 系统时钟状态 SWS[3:2] (只读，用于判断切换是否完成) ---- */
#define CFGR_SWS_HSI (0U << 2) // 系统时钟源为 HSI
#define CFGR_SWS_HSE (1U << 2) // 系统时钟源为 HSE
#define CFGR_SWS_PLL (2U << 2) // 系统时钟源为 PLL
#define CFGR_SWS_Msk (3U << 2) // 系统时钟状态

/* ---- 4.3 AHB 预分频 HPRE[7:4] ---- */
#define CFGR_HPRE_DIV1             (0U << 4)    /* SYSCLK / 1 */
#define CFGR_HPRE_DIV2             (8U << 4)    // AHB 预分频为 2

/* ---- 4.4 APB1 预分频 PPRE1[10:8] ---- */
#define CFGR_PPRE1_DIV1            (0U << 8)   // APB1 预分频为 1
#define CFGR_PPRE1_DIV2            (4U << 8)   // APB1 预分频为 2

/* ---- 4.5 APB2 预分频 PPRE2[13:11] ---- */
#define CFGR_PPRE2_DIV1            (0U << 11)  // APB2 预分频为 1

/* ---- 4.6 PLL 时钟源 PLLSRC[16] ---- */
#define CFGR_PLLSRC_HSI_DIV2       (0U << 16)  // PLL 源为 HSI/2
#define CFGR_PLLSRC_HSE            (1U << 16)  // PLL 源为 HSE

/* ---- 4.7 HSE 不分频 PLLXTPRE[17] ---- */
#define CFGR_PLLXTPRE_DIV1         (0U << 17)  // HSE 不分频

/* ---- 4.8 PLL 倍频 PLLMUL[21:18] ---- */
#define CFGR_PLLMUL6               (4U << 18)  // PLL 倍频为 6 倍
#define CFGR_PLLMUL9               (7U << 18)  // PLL 倍频为 9 倍

/* ---- 4.9 USB 预分频 USBPRE[22] ---- */
#define CFGR_USBPRE_DIV1_5         (1U << 22)  // USB 预分频为 1.5 倍
#define CFGR_USBPRE_DIV1           (0U << 22)  // USB 预分频为 1 倍

/*
 * ---- 4.10 72MHz 配置：一次性写入 CFGR 的组合值 ----
 * 这个组合值设置了：PLL源=HSE, PLL×9, USB/1.5, AHB/1, APB1/2, APB2/1
 * 注意：不包含 SW 字段（SW 单独加），因为我们要先配 PLL 等它锁定再切
 */
#define CFGR_72MHZ_CONFIG (CFGR_PLLSRC_HSE     |     // PLL 源为 HSE
    CFGR_PLLXTPRE_DIV1  |  // HSE 不分频
    CFGR_PLLMUL9        | // PLL 倍频为 9 倍
    CFGR_USBPRE_DIV1_5  |  // USB 预分频为 1.5 倍
    CFGR_PPRE1_DIV2     |  // APB1 预分频为 2
    CFGR_PPRE2_DIV1     |  // APB2 预分频为 1
    CFGR_HPRE_DIV1)       // AHB 预分频为 1

#define CFGR_72MHZ_MASK (CFGR_PLLSRC_HSE | CFGR_PLLXTPRE_DIV1 | CFGR_PLLMUL9 | CFGR_USBPRE_DIV1_5 | CFGR_PPRE1_DIV2 | CFGR_PPRE2_DIV1 | CFGR_HPRE_DIV1)

/* ================================================================== */
/*  5. GPIO 配置常量                                                   */
/*     STM32F103 每个引脚在 CRL/CRH 中占 4bit: CNF[1:0] MODE[1:0]     */
/*     高 2bit = CNF (功能), 低 2bit = MODE (速度)                     */
/* ================================================================== */

/* MODE 字段 (低 2 bit) */
#define GPIO_MODE_IN            (0x0)  // 00, 输入
#define GPIO_Speed_10M       (0x1)  // 01, 输出 10M
#define GPIO_Speed_2M        (0x2)  // 10, 输出 2M
#define GPIO_Speed_50M       (0x3)  // 11, 输出 50M

/* CNF 字段 (高 2 bit)、跟在 MODE 上面 */
#define GPIO_CNF_IN_ANALOG   (0U << 2)  // 00, 模拟输入
#define GPIO_CNF_IN_FLOAT    (1U << 2)  // 01, 浮空输入
#define GPIO_CNF_IN_PUPD     (2U << 2)  // 10, 上拉/下拉输入
#define GPIO_CNF_OUT_PP      (0U << 2)  // 00, 推挽输出
#define GPIO_CNF_OUT_OD      (1U << 2)  // 01, 开 drain 输出
#define GPIO_CNF_OUT_AF_PP   (2U << 2)  // 10, 推挽复用输出
#define GPIO_CNF_OUT_AF_OD   (3U << 2)  // 11, 开漏输出

/*
 * 完整的 4bit 配置值（可以直接 << shift 后写入 CRL/CRH）
 * 命名规则: GPIO_CFG_{功能}_{速度}
 */

 #define GPIO_CFG_ANALOG          (GPIO_CNF_IN_ANALOG | GPIO_MODE_IN)
 #define GPIO_CFG_IN_FLOAT        (GPIO_CNF_IN_FLOAT | GPIO_MODE_IN)
 #define GPIO_CFG_IN_PUPD         (GPIO_CNF_IN_PUPD | GPIO_MODE_IN)
 #define GPIO_CFG_OUT_PP_2M       (GPIO_CNF_OUT_PP | GPIO_Speed_2M)
 #define GPIO_CFG_OUT_PP_10M      (GPIO_CNF_OUT_PP | GPIO_Speed_10M)
 #define GPIO_CFG_OUT_PP_50M      (GPIO_CNF_OUT_PP | GPIO_Speed_50M)
 #define GPIO_CFG_OUT_OD_2M       (GPIO_CNF_OUT_OD | GPIO_Speed_2M)
 #define GPIO_CFG_OUT_OD_10M      (GPIO_CNF_OUT_OD | GPIO_Speed_10M)
 #define GPIO_CFG_OUT_OD_50M      (GPIO_CNF_OUT_OD | GPIO_Speed_50M)
 #define GPIO_CFG_OUT_AF_PP_2M    (GPIO_CNF_OUT_AF_PP | GPIO_Speed_2M) 
 #define GPIO_CFG_OUT_AF_PP_10M   (GPIO_CNF_OUT_AF_PP | GPIO_Speed_10M)
 #define GPIO_CFG_OUT_AF_PP_50M   (GPIO_CNF_OUT_AF_PP | GPIO_Speed_50M)
 #define GPIO_CFG_OUT_AF_OD_2M    (GPIO_CNF_OUT_AF_OD | GPIO_Speed_2M)
 #define GPIO_CFG_OUT_AF_OD_10M   (GPIO_CNF_OUT_AF_OD | GPIO_Speed_10M)
 #define GPIO_CFG_OUT_AF_OD_50M   (GPIO_CNF_OUT_AF_OD | GPIO_Speed_50M)

 /* GPIO 快捷操作宏 */
 #define GPIO_Set(GPIOx,pin)    ((GPIOx)->BSRR = (uint32_t)(1U << (pin)))    /*引脚置高*/
 #define GPIO_Clr(GPIOx,pin)    ((GPIOx)->BRR  = (uint32_t)(1U << (pin)))    /*引脚置低*/
 #define GPIO_Tgl(GPIOx,pin)    ((GPIOx)->ODR ^= (uint32_t)(1U << (pin)))    /*引脚翻转*/
 #define GPIO_OUT(GPIOx,pin,val)  ((val)?GPIO_Set(GPIOx,pin) : GPIO_Clr(GPIOx,pin)) /*引脚输出*/
 #define GPIO_In(GPIOx, pin)       (((GPIOx)->IDR >> (pin)) & 1U)     /*引脚输入*/

 /*标准库封装
 typedef struct {
 uint16_t GPIO_Pin;
 GPIO_ModeTypeDef GPIO_Mode;
 GPIO_SpeedTypeDef GPIO_Speed;
 }GPIO_InitTypeDef;

 typedef enum{
 GPIO_Speed_2M = 1,
 GPIO_Speed_10M,
 GPIO_Speed_50M,
 }GPIO_SpeedTypeDef;

 typedef enum{
 GPIO_MODE_AIN = 0x00;
 GPIO_MODE_FLOAT = 0x04; // 00000100 
 GPIO_MODE_IPU = 0x48;  // 01001000 
 GPIO_MODE_IPD = 0x28;  // 00101000
 GPIO_MODE_PP = 0x10; // 00010000
 GPIO_MODE_OD = 0x14; // 00010100
 GPIO_MODE_AF_PP = 0x18; // 00011000
 GPIO_MODE_AF_OD = 0x1C; // 00011100
 }GPIO_ModeTypeDef;

 GPIO_InitTypeDef GPIO_InitStructure;
 GPIO_InitStructure.GPIO_Pin = GPIO_Pin_x;
 GPIO_InitStructure.GPIO_Mode = GPIO_MODE_x;
 GPIO_InitStructure.GPIO_Speed = GPIO_Speed_x;
 }
 
 void GPIO_Init(GPIO_TypdeDef* GPIOx, GPIO_InitTypeDef* GPIO_InitStruct)
 {
 uint32_t tmp = 0x00,currentpin = 0x00,currentmode = 0x00,pinpos = 0x00;pos = 0x00;
 uint32_t tmpreg = 0x00, pinmask = 0x00;
 currentmode = (uint32_t)(GPIO_InitStruct->GPIO_Mode)&((uint32_t)(0x0F));
 if((((uint32_t)GPIO_InitStruct->GPIO_Mode)&((uint32_t)0x10) == 0x10)
 {
 currentmode |= (uint32_t)GPIO_InitStruct->GPIO_Speed;
  }
 if (((uint32_t)(GPIO_InitStruct->GPIO_Pin))&((uint32_t)(0x00FF))!= (0x00))
   {
 temreg = GPIOx->CRL;
 for(pinpos = 0; pinpos < 8;pinpos++){
 pos = 1U < <pinpos;
 currentpin = (GPIO_InitStruct->GPIO_Pin) & pos;
 if(currentpin == pos){
 pos = pinpos << 2;
 pinmask = ((uint32_t)0x0F) << pos;
 tmp &= ~pinmask;
 tmpreg |= (currentmode << pos);
 if(GPIO_InitStruct->GPIO_MOde == GPIO_Mode_IPU){
 GPIOx->BSRR = 1U << pos}
 else{
 if(GPIO_InitStruct->GPIO_Mode == GPIO_Mode_IPD){
 GPIOx->BRR = 1U << pos;
  }
 ......

/* ================================================================== */
/*  6. AFIO 配置                                                       */
/* ================================================================== */

#define AFIO_SWJ_FULL             (0x00000000)
#define AFIO_SWJ_NOJATG           (2U << 24)
#define AFIO_SWJ_NOJTRST          (1U << 24)
#define AFIO_SWJ_DISABLE          (4U << 24)
#define AFIO_SWJ_Msk              (7U << 24)


/* ================================================================== */
/*  7. NVIC / SCB 配置                                                 */
/* ================================================================== */

/* 优先级分组值 (写入 SCB->AIRCR 的 PRIGROUP 字段) */
#define NVIC_PRIOGROUP_4          (0x00000300U)  /* 4bit 抢占，0bit 子优先级 */
#define NVIC_PRIOGROUP_3          (0x00000400U)  /* 3bit 抢占，1bit 子优先级 */
#define NVIC_PRIOGROUP_2          (0x00000500U)
#define NVIC_PRIOGROUP_1          (0x00000600U)
#define NVIC_PRIOGROUP_0          (0x00000700U)  /* 0bit 抢占，4bit 子优先级 */

/* AIRCR 的 VECTKEY 字段（必须写 0x05FA，否则写入被忽略） */
#define NVIC_AIRCR_KEY            (0x05FA0000U) 

#define TIMEOUT_DEFAULT           0x100000U

#endif /* __REG32F1_H__ */