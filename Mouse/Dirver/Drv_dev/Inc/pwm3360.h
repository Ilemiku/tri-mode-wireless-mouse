#ifndef __PWM3360_H__
#define __PWM3360_H__
#include "reg32f1.h"
#include "srom.h"
#include "spi.h"
/* ============= 寄存器地址 ============= */
#define PWM3360_REG_Product_ID 0x00
#define PWM3360_REG_Revision_ID 0x01
#define PWM3360_REG_Motion 0x02
#define PWM3360_REG_Delta_X_L 0x03
#define PWM3360_REG_Delta_X_H 0x04
#define PWM3360_REG_Delta_Y_L 0x05
#define PWM3360_REG_Delta_Y_H 0x06
#define PWM3360_REG_SQUAL 0x07
#define PWM3360_REG_Raw_Data_Sum 0x08
#define PWM3360_REG_Maximum_Raw_Data 0x09
#define PWM3360_REG_Minimum_Raw_Data 0x0A
#define PWM3360_REG_Shutter_Lower 0x0B
#define PWM3360_REG_Shutter_Upper 0x0C
#define PWM3360_REG_Control 0x0D
#define PWM3360_REG_Config1 0x0F
#define PWM3360_REG_Config2 0x10
#define PWM3360_REG_Angle_Tune 0x11
#define PWM3360_REG_Frame_Capture 0x12
#define PWM3360_REG_SROM_Enable 0x13
#define PWM3360_REG_Run_Downshift 0x14
#define PWM3360_REG_Rest1_Rate_Lower 0x15
#define PWM3360_REG_Rest1_Rate_Upper 0x16
#define PWM3360_REG_Rest1_Downshift 0x17
#define PWM3360_REG_Rest2_Rate_Lower 0x18
#define PWM3360_REG_Rest2_Rate_Upper 0x19
#define PWM3360_REG_Rest2_Downshift 0x1A
#define PWM3360_REG_Rest3_Rate_Lower 0x1B
#define PWM3360_REG_Rest3_Rate_Upper 0x1C
#define PWM3360_REG_Observation 0x24
#define PWM3360_REG_Data_Out_Lower 0x25
#define PWM3360_REG_Data_Out_Upper 0x26
#define PWM3360_REG_Raw_Data_Grab 0x29
#define PWM3360_REG_SROM_ID 0x2A
#define PWM3360_REG_Min_SQ_Run 0x2B
#define PWM3360_REG_Raw_Data_Threshold 0x2C
#define PWM3360_REG_Config5 0x2F
#define PWM3360_REG_Power_Up_Reset 0x3A
#define PWM3360_REG_Shutdown 0x3B
#define PWM3360_REG_Inverse_Product_ID 0x3F
#define PWM3360_REG_LiftCutoff_Tune3 0x41
#define PWM3360_REG_Angle_Snap 0x42
#define PWM3360_REG_LiftCutoff_Tune1 0x4A
#define PWM3360_REG_Motion_Burst 0x50
#define PWM3360_REG_LiftCutoff_Tune_Timeout 0x58
#define PWM3360_REG_LiftCutoff_Tune_MinLen 0x5A
#define PWM3360_REG_SROM_Load_Burst 0x62
#define PWM3360_REG_Lift_Config 0x63
#define PWM3360_REG_Raw_Data_Burst 0x64
#define PWM3360_REG_LiftCutoff_Tune2 0x65

/* ============= Motion 寄存器 (0x02) 位定义 ============= */
#define PWM3360_MOTION_BIT_MOT (1U << 7) // 1=有运动
#define PWM3360_MOTION_BIT_LIFT (1U << 3)

#define PWM3360_MOTION_OPMODE_RUN 0x00   // Run 模式
#define PWM3360_MOTION_OPMODE_REST1 0x02 // Rest1
#define PWM3360_MOTION_OPMODE_REST2 0x04 // Rest2
#define PWM3360_MOTION_OPMODE_REST3 0x06 // Rest3

/* ============= Config2 (0x10) 位定义 ============= */
#define PWM3360_CONFIG2_REST_EN (1U << 5) // Rest 模式使能
#define PWM3360_CONFIG2_RPT_MOD (1U << 2) // 独立 X/Y CPI

/* ============= SROM_Enable (0x13) 命令 ============= */
#define PWM3360_SROM_CMD_INIT 0x1D  // 初始化下载
#define PWM3360_SROM_CMD_START 0x18 // 开始下载
#define PWM3360_SROM_CMD_CRC 0x15   // CRC 自检

/* ============= 其他关键寄存器值 ============= */
#define PWM3360_POWER_UP_RESET_VAL                                                                 \
    0x5A // 写入 **0x5A** 触发完整芯片复位。所有寄存器恢复默认值，SROM 固件丢失。
#define PWM3360_SHUTDOWN_VAL                                                                       \
    0xB6 // 写入 **0xB6** 进入关机模式。唤醒需要写 0x5A 到 Power_Up_Reset + 重新下载 SROM。

/* ============= SROM 固件大小 ============= */
#define PWM3360_SROM_SIZE 4094

extern const u8 PWM3360_SROM_Firmware[PWM3360_SROM_SIZE];
/* ============= CPI 枚举（Config1 值 = CPI/100 - 1） ============= */
typedef enum
{
    PWM3360_CPI_400 = 0x03,  // (400/100-1)
    PWM3360_CPI_800 = 0x07,  // (800/100-1)
    PWM3360_CPI_1600 = 0x0F, // (1600/100-1)
    PWM3360_CPI_3200 = 0x1F, // (3200/100-1)
    PWM3360_CPI_5000 = 0x31, // 默认值
    PWM3360_CPI_6400 = 0x3F,
    PWM3360_CPI_12000 = 0x77,
} PWM3360_CPI_t;

/* ============= 运动数据结构 ============= */
typedef struct
{
    s16 delta_x;  // X 位移（12 位有符号）
    s16 delta_y;  // Y 位移（12 位有符号）
    u8 squal;     // 表面质量 (0~128)
    u8 motion;    // Motion 寄存器原始值
    u8 op_mode;   // 当前模式: 0=Run, 1=Rest1, 2=Rest2, 3=Rest3
    u8 is_motion; // 1=有运动
    u8 is_lifted; // 1=已抬离表面
} PWM3360_MotionData_t;

/* SWD诊断快照，不逐帧打印；rearms统计普通寄存器访问后的Burst重入。 */
typedef struct
{
    u32 bursts, nonzero, rearms;
    s16 last_dx, last_dy;
    u8 raw[12];
} PWM3360_Diagnostics;
extern volatile PWM3360_Diagnostics g_pwm3360_diag;

/* ============= 函数声明 ============= */
int PWM3360_Init(void);
void PWM3360_ReadMotion(PWM3360_MotionData_t *data);
void PWM3360_SetCPI(PWM3360_CPI_t cpi);
void PWM3360_WriteReg(u8 reg_addr, u8 data);
u8 PWM3360_ReadReg(u8 reg_addr);
void PWM3360_MotionBurst(u8 buf[12]);
int PWM3360_DownLoadSROM(void);

#endif
