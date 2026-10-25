#include "pwm3360.h"
#include "systick.h"
#include "srom.h"
#include <stdio.h>

/* 普通寄存器读取会退出Motion Burst；下一次突发前必须写0x50重新启用。
 * 传感器仅由Input任务访问，这个状态不会被UI/网络任务并发修改。 */
static u8 s_motion_burst_ready;
volatile PWM3360_Diagnostics g_pwm3360_diag;

void PWM3360_WriteReg(u8 reg_addr, u8 data)
{
    u8 addr_byte = (reg_addr & 0x7F) | 0x80; // 设置最高位为 1，表示写操作
    s_motion_burst_ready = 0;                /* 配置写入后也保守地重新启用下一轮Burst */

    SPI_NCS_LOW(SPI_ID_1); // 拉低片选，开始通信
    Delay_us(1);           // 确保片选稳定

    SPI_Transfer(SPI_ID_1, addr_byte); // 发送寄存器地址

    SPI_Transfer(SPI_ID_1, data); // 发送数据
    Delay_us(35);                 /* 写操作要求最后时钟之后保持NCS为低，不能只在抬高后延时 */

    SPI_NCS_HIGH(SPI_ID_1); // 拉高片选，结束通信

    Delay_us(200); // 等待至少 200 微秒，确保写入完成

    //printf("reg:%02x w/rite:%02x\r\n",reg_addr,data);
}
/**
 * @brief 读 PWM3360 寄存器
 *
 * 帧: [A6..A0 | 0] [dummy]
 * NCS↓ → 发地址(bit0=0) → 等待 tSRAD → 发 dummy 同时收数据 → NCS↑
 */
u8 PWM3360_ReadReg(u8 reg_addr)
{
    u8 data;
    s_motion_burst_ready = 0; /* 包括DPI查询；下一次运动读取需要重入Burst */

    SPI_NCS_LOW(SPI_ID_1); // 拉低片选，开始通信
    Delay_us(1);

    SPI_Transfer(SPI_ID_1, reg_addr & 0x7F); // 发送寄存器地址，最高位为 0，表示读操作

    Delay_us(160);

    data = SPI_Transfer(SPI_ID_1, 0x00); // 发送 dummy 数据，同时接收寄存器数据

    SPI_NCS_HIGH(SPI_ID_1);
    Delay_us(20);

    return data;
}
/* ==================================================================
 * 突发读取
 * ================================================================== */

/**
 * @brief Motion Burst — 一次读取 12 字节运动数据
 *
 * 字节顺序 (规格书 4.0 节):
 *   [00] Motion       [06] SQUAL
 *   [01] Observation  [07] Raw_Data_Sum
 *   [02] Delta_X_L    [08] Maximum_Raw_Data
 *   [03] Delta_X_H    [09] Minimum_Raw_Data
 *   [04] Delta_Y_L    [10] Shutter_Upper  ← 注意: 先高后低
 *   [05] Delta_Y_H    [11] Shutter_Lower
 */

void PWM3360_MotionBurst(u8 buf[12])
{
    if (!s_motion_burst_ready)
    {
        PWM3360_WriteReg(PWM3360_REG_Motion_Burst, 0x00);
        s_motion_burst_ready = 1;
        g_pwm3360_diag.rearms++;
    }
    SPI_NCS_LOW(SPI_ID_1);
    Delay_us(1);

    SPI_Transfer(SPI_ID_1, PWM3360_REG_Motion_Burst & 0x7F); // 发送 Motion Burst 寄存器地址
    Delay_us(35);

    for (int i = 0; i < 12; i++)
    {
        buf[i] = SPI_Transfer(SPI_ID_1, 0x00);
    }

    Delay_us(1);
    SPI_NCS_HIGH(SPI_ID_1);
    Delay_us(1);
}

/* ==================================================================
 * 功能函数
 * ================================================================== */

void PWM3360_SetCPI(PWM3360_CPI_t cpi)
{
    PWM3360_WriteReg(PWM3360_REG_Config1, (u8)cpi);
}

void PWM3360_PowerUPReset(void)
{
    PWM3360_WriteReg(PWM3360_REG_Power_Up_Reset, PWM3360_POWER_UP_RESET_VAL);
    Delay_ms(50);
}

/**
 * @brief 下载 SROM 固件（官方流程）
 *
 * 步骤:
 *  1. Config2 bit5=0 (禁用 Rest)
 *  2. 写 0x1D 到 SROM_Enable → 初始化
 *  3. 等 10ms
 *  4. 写 0x18 到 SROM_Enable → 开始下载
 *  5. 向 SROM_Load_Burst (0x62) 批量写 SROM 数据
 *  6. 读 SROM_ID (0x2A) 验证
 *  7. Config2 恢复 (0x00=有线 或 0x20=无线)
 *
 * @return 0=成功, -1=SROM_ID=0
 */

int PWM3360_DownLoadSROM(void)
{
    PWM3360_WriteReg(PWM3360_REG_Config2, 0x00); // 禁用 休眠 模式

    PWM3360_WriteReg(PWM3360_REG_SROM_Enable, PWM3360_SROM_CMD_INIT);

    Delay_ms(10);

    PWM3360_WriteReg(PWM3360_REG_SROM_Enable, PWM3360_SROM_CMD_START);

    SPI_NCS_LOW(SPI_ID_1);
    SPI_Transfer(SPI_ID_1, PWM3360_REG_SROM_Load_Burst | 0x80); /* 写命令 0xE2，bit7=1 */
    Delay_us(15); /* 首个SROM数据字节之前同样要留下载间隔 */

    for (int i = 0; i < PWM3360_SROM_SIZE; i++)
    {
        SPI_Transfer(SPI_ID_1, PWM3360_SROM_Firmware[i]); // 连续推，CSN 不抬
        Delay_us(15); /* tSWW：SROM 两次写入间隔 ≥15µs，burst 也要守 */
    }

    Delay_us(35);
    SPI_NCS_HIGH(SPI_ID_1);
    Delay_us(200);

    u8 srom_id = PWM3360_ReadReg(PWM3360_REG_SROM_ID);
    if (srom_id == 0x00)
    {
        printf("ERROR: SROM download failed (ID=0)!\r\n");
        return -1;
    }
    return 0;
}

/**
 * @brief Motion Burst — 一次读取 12 字节运动数据
 *
 * 字节顺序 (规格书 4.0 节):
 *   [00] Motion       [06] SQUAL
 *   [01] Observation  [07] Raw_Data_Sum
 *   [02] Delta_X_L    [08] Maximum_Raw_Data
 *   [03] Delta_X_H    [09] Minimum_Raw_Data
 *   [04] Delta_Y_L    [10] Shutter_Upper  ← 注意: 先高后低
 *   [05] Delta_Y_H    [11] Shutter_Lower
 */
void PWM3360_ReadMotion(PWM3360_MotionData_t *data)
{
    u8 buf[12];
    PWM3360_MotionBurst(buf); // 读取运动数据
                              //motion寄存器数据
    data->motion = buf[0];    // 运动状态
    data->is_motion = (buf[0] & PWM3360_MOTION_BIT_MOT) ? 1 : 0;
    data->is_lifted = (buf[0] & PWM3360_MOTION_BIT_LIFT) ? 1 : 0;
    data->op_mode = (buf[0] >> 1) & 0x03; // 操作模式
                                          //位移数据
    s16 dx = ((s16)buf[3] << 8) | (s16)buf[2];
    s16 dy = ((s16)buf[5] << 8) | (s16)buf[4];
    data->delta_x = dx;
    data->delta_y = dy;
    data->squal = buf[6];
    g_pwm3360_diag.bursts++;
    if (dx || dy)
        g_pwm3360_diag.nonzero++;
    g_pwm3360_diag.last_dx = dx;
    g_pwm3360_diag.last_dy = dy;
    for (u8 i = 0; i < 12; i++)
        g_pwm3360_diag.raw[i] = buf[i];
}
/* ==================================================================
 * 初始化
 * ================================================================== */

/**
 * @brief PWM3360 完整初始化
 *
 * 严格按照官方规格书第 7 节的 Power Up 流程:
 *   上电 → NCS 复位 → Power_Up_Reset → 等50ms → 预读寄存器
 *   → 验证 Product_ID → SROM 下载 → 配置 CPI → 完成
 *
 * @return 0=成功, 负数=失败
 */
int PWM3360_Init(void)
{
    SPI_Init(SPI_ID_1, SPI_MODE_3, SPI_BR_DIV64); /* 72MHz/64=1.125MHz，低于规格书2MHz上限 */

    /* 2. NCS 拉高再拉低，复位 SPI 端口 */
    SPI_NCS_HIGH(SPI_ID_1);
    Delay_ms(1);
    SPI_NCS_LOW(SPI_ID_1);
    Delay_us(10);
    SPI_NCS_HIGH(SPI_ID_1);
    Delay_ms(1);

    /*上电复位*/
    PWM3360_PowerUPReset();

    /* 预读寄存器（规格书要求，忽略 MOTION 引脚状态） */

    PWM3360_ReadReg(PWM3360_REG_Motion);
    PWM3360_ReadReg(PWM3360_REG_Delta_X_L);
    PWM3360_ReadReg(PWM3360_REG_Delta_X_H);
    PWM3360_ReadReg(PWM3360_REG_Delta_Y_L);
    PWM3360_ReadReg(PWM3360_REG_Delta_Y_H);

    /* 验证 SPI 通信 */
    u8 pid = PWM3360_ReadReg(PWM3360_REG_Product_ID);
    u8 rid = PWM3360_ReadReg(PWM3360_REG_Revision_ID);
    u8 ipid = PWM3360_ReadReg(PWM3360_REG_Inverse_Product_ID);

    printf("Product_ID: 0x%02X (expect 0x42), Rev: 0x%02X, "
           "InvID: 0x%02X (expect 0xBD)\r\n",
           pid, rid, ipid);

    if (pid != 0x42)
    {
        printf("ERROR: Wrong Product_ID! Check wiring/power/SPI mode.\r\n");
        return -1;
    }

    if (ipid != 0xBD)
    {
        printf("WARNING: Inverse_Product_ID mismatch, SPI may be unreliable.\r\n");
    }

    /* 下载 SROM */
    if (PWM3360_DownLoadSROM() != 0)
    {
        return -2;
    }

    /* 配置CPI*/
    PWM3360_SetCPI(PWM3360_CPI_1600);

    /*Config2：有线鼠标，禁用休眠模式（rest)；无线模式启用*/
    PWM3360_WriteReg(PWM3360_REG_Config2, 0x00);

    printf("PWM3360 init done.\r\n");
    return 0;
}
