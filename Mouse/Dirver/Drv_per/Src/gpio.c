#include "gpio.h"
void GPIO_Set(GPIO_TypeDef *GPIOx, u32 GPIO_Pin)
{
    GPIOx->BSRR = GPIO_Pin;
}

void GPIO_Reset(GPIO_TypeDef *GPIOx, u32 GPIO_Pin)
{
    GPIOx->BRR = GPIO_Pin;
}

void GPIO_Read(GPIO_TypeDef *GPIOx, u32 GPIO_Pin)
{
    GPIOx->IDR &= GPIO_Pin;
}

typedef struct
{
    GPIO_TypeDef *port;
    u8 pin;
    u8 mode;
    u8 init_val; //输入:0=pd,1为pu/输出 0为L,1为H
} pincfg_t;

static const pincfg_t g_pin_cfg[] = {
    // SPI1
    {GPIOA, 5, GPIO_MODE_OUT_AF_PP_50M, 0}, // SCK
    {GPIOA, 6, GPIO_MODE_IN_FLOATING, 0},   // MISO
    {GPIOA, 7, GPIO_MODE_OUT_AF_PP_50M, 0}, // MOSI
    {GPIOE, 2, GPIO_MODE_OUT_PP_50M, 1},    // CS (高=失能)

    // SI24R1 SPI2
    {GPIOB, 13, GPIO_MODE_OUT_AF_PP_50M, 0}, // SCK
    {GPIOB, 14, GPIO_MODE_IN_FLOATING, 0},   // MISO
    {GPIOB, 15, GPIO_MODE_OUT_AF_PP_50M, 0}, // MOSI
    {GPIOG, 7, GPIO_MODE_OUT_PP_50M, 1},     // CSN
    {GPIOG, 8, GPIO_MODE_OUT_PP_50M, 0},     // CE
    {GPIOG, 6, GPIO_MODE_IN_PUPD, 1},        // IRQ (PU)

    // BLE UART3
    {GPIOB, 10, GPIO_MODE_OUT_AF_PP_50M, 0}, // TX
    {GPIOB, 11, GPIO_MODE_IN_FLOATING, 0},   // RX
    {GPIOA, 15, GPIO_MODE_OUT_PP_50M, 0},    // BRTS (低=唤醒)
    {GPIOA, 4, GPIO_MODE_IN_PUPD, 1},        // STATUS (PU)

    // Debug UART1
    {GPIOA, 9, GPIO_MODE_OUT_AF_PP_50M, 0}, // TX
    {GPIOA, 10, GPIO_MODE_IN_FLOATING, 0},  // RX

    // 按键 (全部 PU, 按下=低)
    {GPIOE, 0, GPIO_MODE_IN_PUPD, 1}, // 左键
    {GPIOE, 6, GPIO_MODE_IN_PUPD, 1}, // 右键
    {GPIOF, 3, GPIO_MODE_IN_PUPD, 1}, // 侧键1
    {GPIOF, 4, GPIO_MODE_IN_PUPD, 1}, // 侧键2
    {GPIOE, 3, GPIO_MODE_IN_PUPD, 1}, // DPI
    {GPIOE, 4, GPIO_MODE_IN_PUPD, 1}, // 切换模式
    {GPIOA, 0, GPIO_MODE_IN_PUPD, 0}, // WK_UP (PD, 按下=高)

    // LED (推挽, 高=灭)
    {GPIOB, 5, GPIO_MODE_OUT_PP_50M, 1}, // LED0
    {GPIOE, 5, GPIO_MODE_OUT_PP_50M, 1}, // LED1

    // TFT FSMC (只列代表, 共 28 个引脚)
    {GPIOD, 0, GPIO_MODE_OUT_AF_PP_50M, 0}, // D0
    // ... PD1-15, PE7-15, PG12 ...
    {GPIOG, 13, GPIO_MODE_OUT_PP_50M, 1},
};

/*........调试...........
char getcharport(GPIO_TypeDef *port){
   if(port == GPIOA) return 'A';
   else if(port == GPIOB) return 'B';
   else if(port == GPIOC) return 'C';
   else if(port == GPIOD) return 'D';
   else if(port == GPIOE) return 'E';
   else if(port == GPIOF) return 'F';
   else if(port == GPIOG) return 'G';
   else return '?';
}
   */
void GPIO_Init(void)
{
    /*    测试串口是否跑通
   printf("GPIO_Init start\n");
*/
    // 使能所有 GPIO 时钟
    RCC_APB2ENR_GPIOAEN();
    RCC_APB2ENR_GPIOBEN();
    RCC_APB2ENR_GPIODEN();
    RCC_APB2ENR_GPIOEEN();
    RCC_APB2ENR_GPIOFEN();
    RCC_APB2ENR_GPIOGEN();

    //禁用jawg,保留swd（释放pa15,pb3,4）
    RCC_APB2ENR_AFIOEN();                            // 使能 AFIO 时钟
    Reg_WR(AFIO->MAPR, AFIO_SWJ_Msk, AFIO_SWJ_OSWD); /*    检测要初始化的引脚数目
 printf("total Pin:%d\n", (u32)(sizeof(g_pin_cfg)/sizeof(g_pin_cfg[0])));
*/
    //初始化所有引脚
    for (u32 i = 0; i < sizeof(g_pin_cfg) / sizeof(g_pin_cfg[0]); i++)
    {
        const pincfg_t *c = &g_pin_cfg[i];

        vu32 shift = ((c->pin) & 0x7) * 4; // 每个引脚占 4 位,配置的CRL和CRH

        vu32 *cr = ((c->pin) < 8) ? &(c->port->CRL) : &(c->port->CRH);

        vu32 tmp = *cr;
        tmp &= ~(0xF << shift);
        tmp |= (c->mode) << shift;
        *cr = tmp;

        if (c->mode == GPIO_MODE_IN_PUPD)
        {
            if (c->init_val)
                (c->port)->BSRR = (1U << (c->pin)); // 上拉
            else
                (c->port)->BRR = (1U << (c->pin)); // 下拉
        }
        else if ((c->mode & 0x3) != 0)
        {
            if (c->init_val)
                (c->port)->BSRR = (1U << (c->pin)); // 高
            else
                (c->port)->BRR = (1U << (c->pin)); // 低
        }
        /*    测试初始化是否成功
      rintf("GPIO:%c pin: %d mode: %d init: %d done\n", getcharport(c->port), c->pin, c->mode, c->init_val);
*/
    }
}
