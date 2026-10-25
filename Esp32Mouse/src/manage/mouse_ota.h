#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* OTA属于控制通路，绝不在UART收帧任务或MQTT回调里下载/擦写Flash。 */
typedef void (*mouse_ota_notify_t)(const char *json);

/* 初始化一次：host/token来自已保存的网络配置，notify只负责发布状态。 */
void MouseOTA_Init(const char *host, const char *token, mouse_ota_notify_t notify);

/* 校验并投递，不在调用者上下文下载。ESP_OK仅代表接受/重复已知请求，
 * 最终是否成功必须继续观察confirmed、failed或rolled_back状态。 */
esp_err_t MouseOTA_Submit(const char *id, const char *url, const char *version, const char *sha256,
                          uint32_t size);

/* MQTT重新连上时重发最近状态；不是重新执行升级。 */
void MouseOTA_PublishStatus(void);
/* 管理任务定期报告本地自检结果：BLE已启动 + 网络在线 + ZE状态新鲜。 */
void MouseOTA_Health(bool healthy);
void MouseOTA_Info(uint32_t *physical_flash, const char **partition, bool *busy);
