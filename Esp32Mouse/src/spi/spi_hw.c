#include "spi_hw.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define SPI_SCK_PIN GPIO_NUM_5
#define SPI_MOSI_PIN GPIO_NUM_7
#define SPI_MISO_PIN GPIO_NUM_6
#define SPI_CS_PIN GPIO_NUM_2

static spi_device_handle_t s_spi_dev; /* 设备句柄：add_device 后拿到，发事务要用 */

/**
 * @brief 初始化 ESP32 硬件 SPI（模式 3，1MHz）
 *
 * 两步：
 *   1. spi_bus_initialize  —— 初始化"总线"：SCK/MOSI/MISO 信号接到哪些引脚（GPIO Matrix）
 *   2. spi_bus_add_device  —— 在这条总线上挂一个"设备"：模式、速率、CS
 * 之后所有收发都通过 spi_device_transmit()，SCK 波形由硬件外设自动产生。
 */
void SPI_Init(void)
{
    /* ---- 第 1 步：总线（相当于原来的 时钟使能 + GPIO 复用配置） ---- */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = SPI_SCK_PIN,
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .quadwp_io_num = -1, /* 四线模式没用，填 -1 */
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* ---- 第 2 步：设备（相当于原来 SPI_CR1 的模式/波特率配置） ---- */
    spi_device_interface_config_t dev_cfg = {
        .mode = 3,                     /* Mode 3: CPOL=1, CPHA=1，和手写版一致 */
        .clock_speed_hz = 1000 * 1000, /* 1MHz，先慢后快 */
        .spics_io_num = -1,            /* -1 = CS 由我们自己控制（保持 pwm3360 的调用方式） */
        .queue_size = 1,               /* 事务队列深度，1 就够了 */
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi_dev));

    /* ---- CS 引脚自己管理：配成输出，默认高（失能） ---- */
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << SPI_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(SPI_CS_PIN, 1);
}

/**
 * @brief 硬件 SPI 收发一字节
 *
 * 一个 spi_transaction_t 描述一"事务"：发什么、收什么、多少位。
 * 硬件外设自动产生 SCK 并移位，我们只负责准备数据、等结果。
 */
uint8_t SPI_Transfer(uint8_t tx)
{
    uint8_t rx = 0;

    spi_transaction_t trans = {
        .length = 8,      /* 总长度 8 位 */
        .tx_buffer = &tx, /* 要发的数据 */
        .rx_buffer = &rx, /* 收到的数据写到这里 */
    };

    ESP_ERROR_CHECK(spi_device_transmit(s_spi_dev, &trans)); // 发一个事务
    return rx;
}

void SPI_CS_HIGH(void)
{
    gpio_set_level(SPI_CS_PIN, 1);
}

void SPI_CS_LOW(void)
{
    gpio_set_level(SPI_CS_PIN, 0);
}
