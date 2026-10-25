#include "mouse.h"
#include "pwm3360.h"
#include <stdio.h>
#include "gpio.h"
#include "si24r1_app.h" /* SI24R1_App_Send_Report：2.4G 通道 */
#include "usb_hw.h"     /* USB_HW_EpTx：有线通道 */
#include "usb_core.h"   /* usb_config_value：枚举完成标志 */
#include "esp32_link.h" /* ESP32_Link_Send_Report：蓝牙通道 */

static u8 mouse_buttons; // bit0~3 = 键1~4
static u8 dpi_level;     // 当前档位下标
static mouse_mode_t mouse_mode = MOUSE_MODE_USB;
/* 档位表：下标即 dpi_level，切档就是"下标+1取模"后写 Config1 */
static const PWM3360_CPI_t dpi_table[] = {PWM3360_CPI_800, PWM3360_CPI_1600, PWM3360_CPI_3200,
                                          PWM3360_CPI_6400};

static void Mouse_BuildReport(const PWM3360_MotionData_t *motion, u8 report[6])
{
    report[0] = mouse_buttons;                       // 按键
    report[1] = (u8)(motion->delta_x & 0xFF);        // X 低字节
    report[2] = (u8)((motion->delta_x >> 8) & 0xFF); // X 高字节
    report[3] = (u8)(motion->delta_y & 0xFF);        // Y 低字节
    report[4] = (u8)((motion->delta_y >> 8) & 0xFF); // Y 高字节
    report[5] = 0x00;                                // 滚轮（暂无硬件）
}

static void Mouse_ModeShow(void)
{
    switch (mouse_mode)
    {
    case MOUSE_MODE_USB:
        GPIO_Reset(GPIOB, GPIO_PIN_5); /* 红亮 */
        GPIO_Set(GPIOE, GPIO_PIN_5);   /* 绿灭 */
        break;
    case MOUSE_MODE_2_4G:
        GPIO_Set(GPIOB, GPIO_PIN_5);   /* 红灭 */
        GPIO_Reset(GPIOE, GPIO_PIN_5); /* 绿亮 */
        break;
    case MOUSE_MODE_BLE:
        GPIO_Reset(GPIOB, GPIO_PIN_5); /* 红亮 */
        GPIO_Reset(GPIOE, GPIO_PIN_5); /* 绿亮 */
        break;
    default:
        break;
    }
}

void Mouse_ModeCycle(void)
{
    /* 加 1 取模循环；MOUSE_MODE_NUM 是枚举，取模前转 int 避免混合类型警告 */
    mouse_mode = (mouse_mode_t)((mouse_mode + 1) % (int)MOUSE_MODE_NUM);
    Mouse_ModeShow();
}

mouse_mode_t Mouse_ModeGet(void)
{
    return mouse_mode;
}

/* 路由：只有当前模式的通道发报告，其余静默 */
void Mouse_ModeSet(const u8 *report)
{
    if (mouse_mode == MOUSE_MODE_2_4G)
    {
        SI24R1_App_Send_Report(report); /* 2.4G 组包发送 */
    }
    else if (mouse_mode == MOUSE_MODE_USB && usb_config_value == 1)
    {
        /* 枚举期间碰 EP1 会把端点状态弄乱，配置完成才发 */
        USB_HW_EpTx(1, report, 6);
    }
    else if (mouse_mode == MOUSE_MODE_BLE)
    {
        ESP32_Link_Send_Report(report); /* 每 10ms 一帧给 ESP32 */
    }
}

void Mouse_Report(u8 report[6])
{
    PWM3360_MotionData_t motion;
    PWM3360_ReadMotion(&motion);
    Mouse_BuildReport(&motion, report);
}

void Mouse_Init(void)
{
    mouse_buttons = 0;
    dpi_level = 2;                        // 与 PWM3360_Init 的 3200 对齐
    PWM3360_SetCPI(dpi_table[dpi_level]); // 显式同步一次
    mouse_mode = MOUSE_MODE_USB;          // 上电默认有线模式
    Mouse_ModeShow();
}

void Mouse_SetButton(u8 btn, u8 pressed)
{
    if (btn < 1 || btn > 4)
        return;
    if (pressed)
        mouse_buttons |= (1U << (btn - 1));
    else
        mouse_buttons &= ~(1U << (btn - 1));
}

void Mouse_DPICycle(void)
{
    u8 next = (u8)((dpi_level + 1U) % 4U);
    (void)Mouse_ApplyDPI((u16)(((u16)dpi_table[next] + 1U) * 100U));
    /* 档位变化由低优先级服务任务输出，输入任务不再执行printf。 */
}

u16 Mouse_ReadActualDPI(u8 *raw)
{
    *raw = PWM3360_ReadReg(PWM3360_REG_Config1);
    return (u16)(((u16)*raw + 1U) * 100U);
}

u8 Mouse_ApplyDPI(u16 dpi)
{
    u8 i, raw;
    for (i = 0; i < 4; ++i)
    {
        if (((u16)dpi_table[i] + 1U) * 100U != dpi)
            continue;
        PWM3360_SetCPI(dpi_table[i]);
        if (Mouse_ReadActualDPI(&raw) != dpi)
            return 2; /* 硬件读回不匹配 */
        dpi_level = i;
        return 0;
    }
    return 1; /* 不支持的档位 */
}

u8 Mouse_GetDPILevel(void)
{
    return dpi_level;
}
