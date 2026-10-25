#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* 临时诊断快照：只观察事件，不强制打开订阅、不改变配对策略。 */
typedef struct
{
    uint32_t auth_events;
    uint32_t auth_failures;
    int32_t last_auth_result; /* -1=尚无结果，0=成功，其余为安全失败原因 */
    uint32_t write_events;
    uint16_t last_write_handle;
    uint16_t last_write_length;
    uint16_t last_write_value; /* 最近一次属性写入的前两字节；不直接断言是CCCD */
    uint32_t complete_events;
    uint32_t complete_failures;
    uint16_t last_complete_status;
    uint16_t mtu;
    uint16_t cccd_handle; /* 输入报告CCCD；0=尚未识别 */
    uint32_t ccc_records; /* 收到并保存的CCCD写入次数 */
    uint32_t ccc_replays; /* 重连后恢复订阅状态的次数 */
} ble_hid_diag_t;

void BLEHID_Init(void);        /* 初始化蓝牙 + HID 设备（广播等连接） */
bool BLEHID_IsConnected(void); /* 查询是否已连接主机 */
bool BLEHID_IsReady(void);     /* HID服务已启动；OTA自检不要求用户恰好已连接蓝牙 */
esp_err_t BLEHID_SendReport(const uint8_t report[6]); /* 返回库的实际发送结果 */
void BLEHID_GetDiagnostics(ble_hid_diag_t *out);

#endif
