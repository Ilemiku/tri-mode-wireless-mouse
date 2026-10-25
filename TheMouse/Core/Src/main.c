#include "main.h"
#include "usb_device.h"
#include "gpio.h"
#include "reg32f1.h"
#include "systick.h"


void SystemClock_Config(void);

int main(void)
{
  SystemInit_Reg(); //设置中断，AFIO/PWR/SWJ

  SystemClock_Config();

  SysTick_Init();

  GOIO_Init_Reg();
  
  MX_USB_DEVICE_Init();

  while (1)
  {

  }
}

void SystemClock_Config(void)
{
  uint32_t timeout;

    RCC->CR |= RCC_CR_HSEON;
    timeout = 0x100000U;
    while(!(RCC->CR & RCC_CR_HSERDY))
    {
      if(--timeout == 0U) goto clk_fail;
    }

    REG_WR(RCC->CFGR,CFGR_72MHZ_MASK,CFGR_72MHZ_CONFIG);
    RCC->CR |= RCC_CR_PLLON;
    timeout = 0x100000U;
    while(!(RCC->CR & RCC_CR_PLLRDY))
    {
      if(--timeout == 0U)goto clk_fail;
    }

   REG_WR(RCC->CFGR,CFGR_SW_Msk,CFGR_SW_PLL);
   timeout = 0x100000U;
   while((RCC->CFGR & CFGR_SWS_Msk) != CFGR_SWS_PLL)
   {
    if(--timeout == 0U) goto clk_fail;
   }

   Systemcoreclock = 72000000U; // 72MHz
   return;

   clk_fail:
   while(1) {
}
}

void SystemInit_Reg(void) // SystemInit
{
  SCB->AIRCR = NVIC_AIRCR_KEY | NVIC_NVIC_PRIOGROUP_4;
  FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;
  CLK_AFIO_ENABLE;
  CLK_PWR_ENABLE;
  REG_WG(AFIO->MAPR, AFIO_SWJ_Msk, AFIO_SWJ_DISABLE);
}

void GPIO_Init_Reg(void)
{
  CLK_GPIOA_ENABLE();
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
