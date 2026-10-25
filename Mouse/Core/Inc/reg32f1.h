#ifndef __REG32F1_H__
#define __REG32F1_H__
#include <stdint.h>

typedef volatile uint64_t vu64;
typedef volatile uint32_t vu32;
typedef volatile uint16_t vu16;
typedef volatile uint8_t vu8;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

// GPIOB: 0X4001 0C00   GPIOE: 0x4001 1800
// RCC:0x4002 1000

/*...................操作宏...................................*/
#define Reg_Set(reg, bits) ((reg) |= (u32)(bits))

#define Reg_Clr(reg, bits) ((reg) &= ~(u32)(bits))

#define Reg_WR(reg, clr, set) ((reg) = (((reg) & ~(u32)(clr)) | (u32)(set)))

#define Reg_RD(reg, bits) ((reg) & (u32)bits)

/*....................FLASH.....................................*/
#define FLASH_BASE 0x40022000UL
#define FLASH_ACR (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_LATENCY_2WS (1U << 1) // 时钟延时2个时钟周期
#define FLASH_ACR_HLFCYA (0U << 3)      //内存半周期使能关闭
#define FLASH_ACR_PRFTBE (1U << 4)      // Flash 缓存使能
#define FLASH_ACR_PRFTBS_Msk (1U << 5)  // Flash 缓存使能状态掩码
#define FLASH_ACR_CFG (FLASH_ACR_HLFCYA | FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2WS)

typedef struct
{
    vu32 ACR;
    vu32 KEYR;
    vu32 OPTKEYR;
    vu32 SR;
    vu32 CR;
    vu32 AR;
    vu32 _pad;
    vu32 OBR;
    vu32 WRPR;
} FLASH_TypeDef;

/*....................RCC+外设使能.....................................*/
#define APB1_PERIPH_BASE 0x40000000UL
#define APB2_PERIPH_BASE 0x40010000UL
#define AHBPERIPH_BASE 0x40020000UL
#define RCC_BASE 0x40021000UL
#define RCC ((RCC_TypeDef *)RCC_BASE)

typedef struct
{
    vu32 CR;
    vu32 CFGR;
    vu32 CIR;
    vu32 APB2RSTR;
    vu32 APB1RSTR;
    vu32 AHBENR;
    vu32 APB2ENR;
    vu32 APB1ENR;
    vu32 BDCR;
    vu32 CSR;
    vu32 RESERVED;
} RCC_TypeDef;

/*-----------------RCC寄存器----------------------------------*/
#define RCC_CR_HSEON (1U << 16)
#define RCC_CR_HSERDY (1U << 17)
/* ---- 系统时钟选择 SW[1:0] (只读，用于判断切换是否完成) ---- */
#define RCC_CFGR_SW_HSE (1U << 0) // 系统时钟源为 HSE
#define RCC_CFGR_SW_PLL (2U << 0) // 系统时钟源为 PLL
#define RCC_CFGR_SW_HSI (0U << 0) // 系统时钟源为 HSI
#define RCC_CFGR_SW_Msk (3U << 0)
/* ---- 系统时钟状态 SWS[3:2] (只读，用于判断切换是否完成) ---- */
#define RCC_CFGR_SWS_HSI (0U << 2) // 系统时钟源为 HSI
#define RCC_CFGR_SWS_HSE (1U << 2) // 系统时钟源为 HSE
#define RCC_CFGR_SWS_PLL (2U << 2) // 系统时钟源为 PLL
#define RCC_CFGR_SWS_Msk (3U << 2) // 系统时钟状态
/* ---- 时钟设置 ---- */
#define RCC_CFGR_HPRE_DIV1 (0U << 4)

#define RCC_CFGR_PPRE1_DIV2 (4U << 8)

#define RCC_CFGR_PPRE2_DIV1 (0U << 11)
/*...............PLL配置........................................*/
#define RCC_CR_PLLON (1U << 24)
#define RCC_CR_PLLRDY (1U << 25)
//PLL 输入时钟源
#define RCC_CFGR_PLLSRC_HSE (1U << 16)
//PLL 输入时钟分频
#define CFGR_PLLXTPRE_DIV1 (0U << 17) // HSE 不分频
//PLL 输入时钟分频
#define RCC_CFGR_PLLXTPRE_DIV1 (0U << 17)
//PLL 输出时钟倍频
#define RCC_CFGR_PLLMULL9 (7U << 18)
//USB 时钟
#define RCC_CFGR_USBPRE_DIV1_5 (0U << 22)

/*.............RCC掩码..............................................*/
//CFGR
#define RCC_CFGR_SW_Msk (3U << 0)        // SW[1:0]      : 可读写，系统时钟选择
#define RCC_CFGR_HPRE_Msk (0xF << 4)     // HPRE[7:4]    : AHB 预分频
#define RCC_CFGR_PPRE1_Msk (7U << 8)     // PPRE1[10:8]  : APB1 预分频
#define RCC_CFGR_PPRE2_Msk (7U << 11)    // PPRE2[13:11] : APB2 预分频
#define RCC_CFGR_PLLSRC_Msk (1U << 16)   // PLLSRC[16]   : PLL 输入源
#define RCC_CFGR_PLLXTPRE_Msk (1U << 17) // PLLXTPRE[17] : HSE 预分频
#define RCC_CFGR_PLLMUL_Msk (0xF << 18)  // PLLMUL[21:18]: PLL 倍频
#define RCC_CFGR_USBPRE_Msk (1U << 22)   // USBPRE[22]   : USB 预分频

#define RCC_CFGR_72MHZ_CONFIG                                                                      \
    (RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLXTPRE_DIV1 | RCC_CFGR_PLLMULL9 | RCC_CFGR_HPRE_DIV1 |       \
     RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_USBPRE_DIV1_5)

/*...........................................NVIC.............................................................*/
#define NVIC_BASE (0xE000E100UL)
#define NVIC ((NVIC_TypeDef *)NVIC_BASE)

typedef struct
{
    vu32 ISER[3];
    vu32 _pad0[29]; // 中断使能寄存器
    vu32 ICER[3];
    vu32 _pad1[29];
    vu32 ISPR[3];
    vu32 _pad2[29]; // 中断 pending寄存器
    vu32 ICPR[3];
    vu32 _pad3[29];
    vu32 IABR[3];
    vu32 _pad4[61]; // 中断屏蔽寄存器
    vu8 IPR[84];
} NVIC_TypeDef;

/*...........................................GPIO..............................................................*/
#define RCC_APB2ENR_GPIOAEN() RCC->APB2ENR |= (1U << 2)
#define RCC_APB2ENR_GPIOBEN() RCC->APB2ENR |= (1U << 3)
#define RCC_APB2ENR_GPIOCEN() RCC->APB2ENR |= (1U << 4)
#define RCC_APB2ENR_GPIODEN() RCC->APB2ENR |= (1U << 5)
#define RCC_APB2ENR_GPIOEEN() RCC->APB2ENR |= (1U << 6)
#define RCC_APB2ENR_GPIOFEN() RCC->APB2ENR |= (1U << 7)
#define RCC_APB2ENR_GPIOGEN() RCC->APB2ENR |= (1U << 8)

#define GPIOA_BASE (APB2_PERIPH_BASE + 0x0800)
#define GPIOB_BASE (APB2_PERIPH_BASE + 0x0C00)
#define GPIOC_BASE (APB2_PERIPH_BASE + 0x1000)
#define GPIOD_BASE (APB2_PERIPH_BASE + 0x1400)
#define GPIOE_BASE (APB2_PERIPH_BASE + 0x1800)
#define GPIOF_BASE (APB2_PERIPH_BASE + 0x1C00)
#define GPIOG_BASE (APB2_PERIPH_BASE + 0x2000)

#define GPIOA ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC ((GPIO_TypeDef *)GPIOC_BASE)
#define GPIOD ((GPIO_TypeDef *)GPIOD_BASE)
#define GPIOE ((GPIO_TypeDef *)GPIOE_BASE)
#define GPIOF ((GPIO_TypeDef *)GPIOF_BASE)
#define GPIOG ((GPIO_TypeDef *)GPIOG_BASE)

#define GPIO_PIN_0 (1U << 0)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_2 (1U << 2)
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_7 (1U << 7)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_13 (1U << 13)
#define GPIO_PIN_14 (1U << 14)
#define GPIO_PIN_15 (1U << 15)
#define GPIO_PIN_All 0xFFFFFFFF

typedef struct
{
    vu32 CRL;
    vu32 CRH;
    vu32 IDR;
    vu32 ODR;
    vu32 BSRR;
    vu32 BRR;
    vu32 LCKR;
} GPIO_TypeDef;

#define GPIO_MODE_IN (0x0)   // 00, 输入
#define GPIO_Speed_10M (0x1) // 01, 输出 10M
#define GPIO_Speed_2M (0x2)  // 10, 输出 2M
#define GPIO_Speed_50M (0x3) // 11, 输出 50M

#define GPIO_MODE_IN_FLOATING (1U << 2) // 01, 浮空输入
#define GPIO_MODE_IN_AF (0U << 2)       // 00, 模拟输入
#define GPIO_MODE_IN_PUPD (2U << 2)     // 01

#define GPIO_MODE_OUT_PP (0x00)
#define GPIO_MODE_OUT_OD (0x04)                                  // CNF=01 开漏, MODE=00
#define GPIO_MODE_OUT_PP_50M (GPIO_MODE_OUT_PP | GPIO_Speed_50M) // 0x03
#define GPIO_MODE_OUT_OD_50M (GPIO_MODE_OUT_OD | GPIO_Speed_50M) // 0x07
#define GPIO_MODE_OUT_AF_PP_50M (0x08 | GPIO_Speed_50M)          // 0x0B, CNF=10 复用推挽
#define GPIO_MODE_Msk (0x3U << 2)

/*................................AFIO.............................................................*/
#define AFIO_BASE (APB2_PERIPH_BASE + 0x0000)
#define RCC_APB2ENR_AFIOEN() RCC->APB2ENR |= (1U << 0)
#define AFIO ((AFIO_TypeDef *)AFIO_BASE)

#define AFIO_SWJ_OSWD (2U << 24) //开启SWD
#define AFIO_SWJ_Msk (7U << 24)

typedef struct
{
    vu32 EVCR;
    vu32 MAPR;
    vu32 EXTICR[4];
    vu32 MAPR2;
} AFIO_TypeDef;
/*...............................串口..............................................*/
#define RCC_APB2ENR_USART1EN() RCC->APB2ENR |= (1U << 14)
#define RCC_APB1ENR_USART3EN() RCC->APB1ENR |= (1U << 18)

#define USART1_BASE (APB2_PERIPH_BASE + 0x3800)
#define USART3_BASE (APB1_PERIPH_BASE + 0x4800)

#define USART1 ((USART_TypeDef *)USART1_BASE)
#define USART3 ((USART_TypeDef *)USART3_BASE)

#define USART_SR_RXNE (1U << 5) //接收非空
#define USART_SR_TXE (1U << 7)  //发送非空
#define USART_CR1_RE (1U << 2)  //接收使能
#define USART_CR1_TE (1U << 3)  //发送使能
#define USART_CR1_UE (1U << 13) //使能

typedef struct
{
    vu16 SR;
    vu16 _pad0;
    vu16 DR;
    vu16 _pad1;
    vu16 BRR;
    vu16 _pad2;
    vu16 CR1;
    vu16 _pad3;
    vu16 CR2;
    vu16 _pad4;
    vu16 CR3;
} USART_TypeDef;
/*................................SPI..........................................................*/
#define SPI1_BASE (APB2_PERIPH_BASE + 0x3000)
#define SPI2_BASE (APB1_PERIPH_BASE + 0x3800)
#define SPI1 ((SPI_TypeDef *)SPI1_BASE)
#define SPI2 ((SPI_TypeDef *)SPI2_BASE)

/* SPI 控制寄存器位（SPI1/SPI2 共用） */
#define SPI_CR1_CPHA (1U << 0) /* 时钟相位 */
#define SPI_CR1_CPOL (1U << 1) /* 时钟极性 */
#define SPI_CR1_MSTR (1U << 2) /* 主模式 */
#define SPI_CR1_SPE (1U << 6)  /* SPI 使能 */
#define SPI_CR1_SSI (1U << 8)  /* 内部从机选择 */
#define SPI_CR1_SSM (1U << 9)  /* 软件从机管理 */

/* SPI 状态寄存器位 */
#define SPI_SR_RXNE (1U << 0) /* 接收非空 */
#define SPI_SR_TXE (1U << 1)  /* 发送空 */

typedef struct
{
    vu16 CR1;
    vu16 pad_0;
    vu16 CR2;
    vu16 pad_1;
    vu16 SR;
    vu16 pad_2;
    vu16 DR;
    vu16 pad_3;
    vu16 CRCPR;
    vu16 pad_4;
    vu16 RXCRCR;
    vu16 pad_5;
    vu16 TXCRCR;
    vu16 pad_6;
    vu16 I2SCFGR;
    vu16 pad_7;
    vu16 I2SPR;
    vu16 pad_8;
} SPI_TypeDef;

/*................................USB..........................................................*/
#define RCC_APB1ENR_USBEN() RCC->APB1ENR |= (1U << 23)    // 使能USB
#define RCC_APB1RSTR_USBRST() RCC->APB1RSTR |= (1U << 23) // 复位USB
#define USB_BASE (APB1_PERIPH_BASE + 0x5C00)
#define USB ((USB_TypeDef *)USB_BASE)

typedef struct
{
    vu16 EP0R;
    vu16 _pad0;
    vu16 EP1R;
    vu16 _pad1;
    vu16 EP2R;
    vu16 _pad2;
    vu16 EP3R;
    vu16 _pad3;
    vu16 EP4R;
    vu16 _pad4;
    vu16 EP5R;
    vu16 _pad5;
    vu16 EP6R;
    vu16 _pad6;
    vu16 EP7R;
    vu16 _pad7;
    vu32 reserved[8];
    vu16 CNTR;
    vu16 _pad9; // 0x40
    vu16 ISTR;
    vu16 _pad10; // 0x44
    vu16 FNR;
    vu16 _pad11; // 0x48
    vu16 DADDR;
    vu16 _pad12; // 0x4C
    vu16 BTABLE; // 0x50
} USB_TypeDef;

#define USB_CNTR_PDWN (1U << 1) //打开断电模式，实际使用需要清零
#define USB_CNTR_FRES (1U << 0) //断电复位

#define USB_CNTR_RESETM (1U << 10) // 复位中断使能
#define USB_CNTR_CTRM (1U << 15)   // 正确传输中断使能

#define USB_EP0R_CTR_RX (1U << 15) // 接收端点控制
#define USB_EP0R_CTR_TX (1U << 7)  // 发送端点控制
#define USB_EP0R_SETUP (1U << 11)  // 设置包
typedef enum
{
    EP_STAT_DISABLED = 0,
    EP_STAT_STALL = 1,
    EP_STAT_NAK = 2,
    EP_STAT_VALID = 3
} USB_EP_stat_t;

#endif
