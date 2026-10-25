#include "Led.h"

// PB5=LED0(红灯), PE5=LED1(绿灯), 高电平=灭, 低电平=亮
// 注意: GPIO_Init() 已配置引脚模式, 这里只确保初始状态为熄灭
void Led_Init(void)
{
    // 确保两个 LED 初始状态为熄灭 (高电平)
    GPIO_Set(GPIOB, GPIO_PIN_5);
    GPIO_Set(GPIOE, GPIO_PIN_5);
}
void Led_On(void)
{
    GPIO_Reset(GPIOB, GPIO_PIN_5);
}

void Led_Off(void)
{
    GPIO_Set(GPIOB, GPIO_PIN_5);
}
