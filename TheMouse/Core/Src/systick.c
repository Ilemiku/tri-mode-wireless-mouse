    #include "systick.h"
    #include "main.h"
    volatile uint32_t g_ms_tick = 0;
    
    void SysTick_Init(void)
    {
       SysTick->LOAD = 72000-1;  // 1ms
       SysTick->VAL = 0; // clear
       SysTick->CTRL = 0x00000007; // enable, clock source
    }

    void SysTick_Handler(void)
    {
       g_ms_tick++; // increment ms tick
    }

    void delay_ms(uint32_t ms)
    {
       uint32_t target = g_ms_tick;
       while((g_ms_tick - target)< ms){ };
    }

    void delay_us(uint32_t us)
    {
      uint32_t load = SysTick ->LOAD;
      uint32_t start = SysTick ->VAL;
      uint32_t ticks =(uint64_t)us * (SystemCoreClock / 1000000U);

      if(ticks > load)ticks = load;

      while(1)
      {
         uint32_t now = SysTick->VAL;
         uint32_t diff;

         if(now < start)
            diff = start - now;
         else
         diff = (load - now) + start + 1U;

         if(diff >= ticks)break;
      }
    }
