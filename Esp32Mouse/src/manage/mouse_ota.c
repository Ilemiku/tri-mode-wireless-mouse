#include "mouse_ota.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "cJSON.h"

/* ESP32应用升级模块。阅读顺序：Init -> Submit -> ota_worker_task -> Health。
 *
 * MQTT回调只投递请求，专用任务负责HTTPS下载和写备用槽。
 * 下载100%不是升级成功；切槽重启后，健康检查通过才确认新版本。
 * 本模块不升级ZE/C8T6，不在普通OTA过程中改bootloader、分区表或NVS布局。
 * 公共CA可进入固件，CA私钥/服务器私钥不可以。TLS仍核对服务器身份。
 */
extern const char ota_ca[] asm("_binary_ca_pem_start");
typedef struct
{
    char id[49];      /* 业务请求ID，用于跨重启关联最终结果 */
    char url[192];    /* 只允许项目配置的HTTPS主机与摘要路径 */
    char version[32]; /* 必须与下载镜像中的应用描述一致 */
    char sha[65];     /* 文件SHA256十六进制文本，加结尾0 */
    uint32_t size;    /* 下载文件字节数，不能超过备用槽 */
} OtaRequest;
typedef struct
{
    char id[49]; /* 此结构已存入NVS，不能随意调整字段布局 */
    char version[32];
} BootRecord;
static const char *TAG = "MOUSE_OTA";
static QueueHandle_t s_queue;
static mouse_ota_notify_t s_notify;
static char s_host[64], s_token[65], s_status[512];
static OtaRequest s_last;
static BootRecord s_boot;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy, s_pending_verify;
static uint32_t s_flash, s_started, s_healthy_since;
static const esp_partition_t *s_running;
static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void MouseOTA_PublishStatus(void)
{
    char copy[sizeof(s_status)];
    portENTER_CRITICAL(&s_lock);
    memcpy(copy, s_status, sizeof(copy));
    portEXIT_CRITICAL(&s_lock);
    if (copy[0] && s_notify)
        s_notify(copy);
}
static void publish_ota_status(const char *id, const char *version, const char *phase, int percent,
                               const char *detail)
{
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return;
    cJSON_AddStringToObject(j, "id", id);
    cJSON_AddStringToObject(j, "version", version);
    cJSON_AddStringToObject(j, "phase", phase);
    cJSON_AddStringToObject(j, "detail", detail);
    cJSON_AddStringToObject(j, "running_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(j, "partition", s_running ? s_running->label : "unknown");
    cJSON_AddNumberToObject(j, "percent", percent);
    cJSON_AddNumberToObject(j, "uptime_ms", uptime_ms());
    char *json = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (json)
    {
        portENTER_CRITICAL(&s_lock);
        snprintf(s_status, sizeof(s_status), "%s", json);
        portEXIT_CRITICAL(&s_lock);
        cJSON_free(json);
        MouseOTA_PublishStatus();
    }
    ESP_LOGI(TAG, "%s %d%%: %s", phase, percent, detail);
}
static esp_err_t save_boot_record(const BootRecord *record)
{
    nvs_handle_t n;
    esp_err_t e = nvs_open("mouse_ota", NVS_READWRITE, &n);
    if (e == ESP_OK)
    {
        e = record ? nvs_set_blob(n, "trial", record, sizeof(*record)) : nvs_erase_key(n, "trial");
        if (e == ESP_ERR_NVS_NOT_FOUND)
            e = ESP_OK;
        if (e == ESP_OK)
            e = nvs_commit(n);
        nvs_close(n);
    }
    return e;
}
static bool is_safe_identifier(const char *s, size_t capacity)
{
    if (!s || !s[0] || strlen(s) >= capacity)
        return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '.' || *s == '-' || *s == '_'))
            return false;
    return true;
}
esp_err_t MouseOTA_Submit(const char *id, const char *url, const char *version, const char *sha,
                          uint32_t size)
{
    if (!s_queue || !is_safe_identifier(id, sizeof(s_last.id)) ||
        !is_safe_identifier(version, sizeof(s_last.version)) || !url ||
        strlen(url) >= sizeof(s_last.url) || !sha || strlen(sha) != 64 || size < 288)
        return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 64; i++)
        if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f')))
            return ESP_ERR_INVALID_ARG;
    char allowed[192];
    snprintf(allowed, sizeof(allowed), "https://%s:8089/firmware/%s.bin", s_host, sha);
    /* 不下载任意URL，不跟随重定向，不把认证令牌交给其他网站。 */
    if (strcmp(url, allowed))
        return ESP_ERR_INVALID_ARG;
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next || next->address == s_running->address || size > next->size)
        return ESP_ERR_INVALID_SIZE;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(s_running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
        return ESP_ERR_INVALID_STATE;
    OtaRequest req = {0};
    snprintf(req.id, sizeof(req.id), "%s", id);
    snprintf(req.url, sizeof(req.url), "%s", url);
    snprintf(req.version, sizeof(req.version), "%s", version);
    snprintf(req.sha, sizeof(req.sha), "%s", sha);
    req.size = size;
    portENTER_CRITICAL(&s_lock);
    bool same_id = strcmp(id, s_last.id) == 0,
         same_payload = same_id && memcmp(&req, &s_last, sizeof(req)) == 0;
    bool occupied = s_busy;
    if (!same_id && !occupied && strcmp(version, esp_app_get_description()->version) != 0)
    {
        s_last = req;
        s_busy = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (same_payload)
    {
        MouseOTA_PublishStatus();
        return ESP_OK;
    }
    if (same_id)
        return ESP_ERR_INVALID_ARG;
    if (occupied)
        return ESP_ERR_TIMEOUT;
    if (strcmp(version, esp_app_get_description()->version) == 0)
        return ESP_ERR_INVALID_VERSION;
    publish_ota_status(id, version, "queued", 0, "request_accepted_not_installed");
    if (xQueueSend(s_queue, &req, 0) != pdPASS)
    {
        portENTER_CRITICAL(&s_lock);
        s_busy = false;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void ota_worker_task(void *arg)
{
    /* 分为建立连接、检查镜像头、写备用槽、核对摘要、切换启动五步。
     * 任一步失败均走done清理；未成功切换前，当前运行槽保持不变。 */
    (void)arg;
    OtaRequest req;
    while (xQueueReceive(s_queue, &req, portMAX_DELAY) == pdPASS)
    {
        esp_err_t err = ESP_FAIL;
        const char *reason = "download_failed";
        esp_http_client_handle_t http = NULL;
        esp_ota_handle_t handle = 0;
        bool ota_open = false;
        uint8_t *buffer = malloc(4096);
        psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        publish_ota_status(req.id, req.version, "connecting", 0, "TLS_verify_server_and_download");
        if (!buffer || !next)
        {
            reason = "no_memory_or_partition";
            goto done;
        }
        if (psa_crypto_init() != PSA_SUCCESS ||
            psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS)
        {
            reason = "sha_init_failed";
            goto done;
        }
        esp_http_client_config_t cfg = {.url = req.url,
                                        .cert_pem = ota_ca,
                                        .timeout_ms = 10000,
                                        .buffer_size = 4096,
                                        .disable_auto_redirect = true,
                                        .keep_alive_enable = true};
        http = esp_http_client_init(&cfg);
        if (!http)
        {
            reason = "http_init_failed";
            goto done;
        }
        char authorization[80];
        snprintf(authorization, sizeof(authorization), "Bearer %s", s_token);
        if (esp_http_client_set_header(http, "Authorization", authorization) != ESP_OK)
            goto done;
        if (esp_http_client_open(http, 0) != ESP_OK)
        {
            reason = "TLS_or_network_failed";
            goto done;
        }
        int64_t length = esp_http_client_fetch_headers(http);
        if (esp_http_client_get_status_code(http) != 200 || length != req.size)
        {
            reason = "http_status_or_size_mismatch";
            goto done;
        }
        uint32_t received = 0, started = uptime_ms();
        int last_percent = -1;
        /* 先读完整镜像头，不假设TCP一次read必然给够288字节。 */
        int header_bytes = 0;
        while (header_bytes < 288)
        {
            int n = esp_http_client_read(http, (char *)buffer + header_bytes, 288 - header_bytes);
            if (n <= 0)
            {
                reason = "truncated_header";
                goto done;
            }
            header_bytes += n;
        }
        esp_image_header_t image;
        esp_app_desc_t app;
        memcpy(&image, buffer, sizeof(image));
        memcpy(&app, buffer + sizeof(image) + sizeof(esp_image_segment_header_t), sizeof(app));
        if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.chip_id != ESP_CHIP_ID_ESP32S3 ||
            app.magic_word != ESP_APP_DESC_MAGIC_WORD ||
            memcmp(app.project_name, esp_app_get_description()->project_name,
                   sizeof(app.project_name)) ||
            strncmp(app.version, req.version, sizeof(app.version)))
        {
            reason = "wrong_chip_project_or_version";
            goto done;
        }
        /* 按4KiB逐块擦写备用槽，绝不写运行中的槽或NVS。 */
        err = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &handle);
        if (err != ESP_OK)
        {
            reason = "ota_begin_failed";
            goto done;
        }
        ota_open = true;
        int n = header_bytes;
        for (;;)
        {
            if (n > 0)
            {
                if (received + (uint32_t)n > req.size)
                {
                    reason = "image_too_large";
                    goto done;
                }
                if (psa_hash_update(&hash, buffer, (size_t)n) != PSA_SUCCESS)
                {
                    reason = "sha_update_failed";
                    goto done;
                }
                err = esp_ota_write(handle, buffer, (size_t)n);
                if (err != ESP_OK)
                {
                    reason = "flash_write_failed";
                    goto done;
                }
                received += (uint32_t)n;
                int percent = (int)((uint64_t)received * 100 / req.size);
                if (percent / 5 != last_percent / 5 || last_percent < 0)
                {
                    last_percent = percent;
                    publish_ota_status(req.id, req.version, "downloading", percent,
                                       "writing_inactive_slot");
                }
            }
            if (received == req.size)
                break;
            if ((uint32_t)(uptime_ms() - started) > 300000)
            {
                reason = "total_download_timeout";
                goto done;
            }
            n = esp_http_client_read(http, (char *)buffer, 4096);
            if (n <= 0)
            {
                reason = "download_interrupted";
                goto done;
            }
            vTaskDelay(
                pdMS_TO_TICKS(1)); /* 给蓝牙和管理任务让出CPU，不等于消除Flash擦写的全部抖动。 */
        }
        uint8_t digest[32];
        size_t digest_len = 0;
        char hex[65];
        if (psa_hash_finish(&hash, digest, sizeof(digest), &digest_len) != PSA_SUCCESS ||
            digest_len != 32)
        {
            reason = "sha_finish_failed";
            goto done;
        }
        for (unsigned i = 0; i < 32; i++)
            snprintf(hex + 2 * i, 3, "%02x", digest[i]);
        if (strcmp(hex, req.sha))
        {
            reason = "sha256_mismatch_old_firmware_kept";
            goto done;
        }
        publish_ota_status(req.id, req.version, "verifying", 100,
                           "SHA256_matches_checking_ESP_image");
        err = esp_ota_end(handle);
        ota_open = false;
        if (err != ESP_OK)
        {
            reason = "ESP_image_validation_failed";
            goto done;
        }
        BootRecord record = {0};
        snprintf(record.id, sizeof(record.id), "%s", req.id);
        snprintf(record.version, sizeof(record.version), "%s", req.version);
        err = save_boot_record(&record);
        if (err != ESP_OK)
        {
            reason = "trial_record_save_failed";
            goto done;
        }
        err = esp_ota_set_boot_partition(next);
        if (err != ESP_OK)
        {
            (void)save_boot_record(NULL);
            reason = "boot_selection_failed";
            goto done;
        }
        publish_ota_status(req.id, req.version, "rebooting", 100,
                           "image_verified_waiting_new_firmware_self_test");
        esp_http_client_cleanup(http);
        free(buffer);
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
    done:
        if (ota_open)
            esp_ota_abort(handle);
        if (http)
            esp_http_client_cleanup(http);
        psa_hash_abort(&hash);
        free(buffer);
        publish_ota_status(req.id, req.version, "failed", 0, reason);
        portENTER_CRITICAL(&s_lock);
        s_busy = false;
        portEXIT_CRITICAL(&s_lock);
    }
}

void MouseOTA_Health(bool healthy)
{
    /* 只处理新镜像的试运行。普通已确认版本不会每90秒重启。 */
    if (!s_pending_verify)
        return;
#if MOUSE_OTA_TEST_ROLLBACK
    /* 仅专门的-rollback-test构建启用；正常固件编译为0，不提供远程故障开关。
     * 故意不确认新应用，用实物验证90秒自检超时后的自动回滚。 */
    healthy = false;
#endif
    uint32_t now = uptime_ms();
    if (!healthy)
        s_healthy_since = 0;
    else if (!s_healthy_since)
        s_healthy_since = now;
    if (s_healthy_since && (uint32_t)(now - s_healthy_since) >= 3000)
    {
        esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        if (e == ESP_OK)
        {
            s_pending_verify = false;
            publish_ota_status(s_boot.id, esp_app_get_description()->version, "confirmed", 100,
                               "BLE_network_ZE_self_test_passed");
            (void)save_boot_record(NULL);
        }
    }
    else if ((uint32_t)(now - s_started) > 90000)
    {
        publish_ota_status(s_boot.id, s_boot.version, "rollback_requested", 0,
                           "new_firmware_self_test_timeout");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();
        if (e != ESP_OK)
        {
            s_pending_verify = false;
            publish_ota_status(s_boot.id, s_boot.version, "failed", 0,
                               "no_bootable_rollback_slot_manual_recovery_needed");
        }
    }
}
void MouseOTA_Info(uint32_t *flash, const char **partition, bool *busy)
{
    *flash = s_flash;
    *partition = s_running ? s_running->label : "unknown";
    portENTER_CRITICAL(&s_lock);
    *busy = s_busy || s_pending_verify;
    portEXIT_CRITICAL(&s_lock);
}
void MouseOTA_Init(const char *host, const char *token, mouse_ota_notify_t notify)
{
    s_notify = notify;
    snprintf(s_host, sizeof(s_host), "%s", host);
    snprintf(s_token, sizeof(s_token), "%s", token);
    s_running = esp_ota_get_running_partition();
    s_started = uptime_ms();
    ESP_ERROR_CHECK(esp_flash_get_physical_size(NULL, &s_flash));
    nvs_handle_t n;
    size_t size = sizeof(s_boot);
    if (nvs_open("mouse_ota", NVS_READONLY, &n) == ESP_OK)
    {
        if (nvs_get_blob(n, "trial", &s_boot, &size) != ESP_OK || size != sizeof(s_boot))
            memset(&s_boot, 0, sizeof(s_boot));
        nvs_close(n);
    }
    s_boot.id[48] = 0;
    s_boot.version[31] = 0;
    esp_ota_img_states_t state;
    s_pending_verify = esp_ota_get_state_partition(s_running, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_PENDING_VERIFY;
#if MOUSE_OTA_TEST_ROLLBACK
    ESP_LOGW(TAG,
             "TEST ONLY: health confirmation intentionally disabled; expect automatic rollback");
#endif
    if (s_pending_verify)
        publish_ota_status(s_boot.id, s_boot.version, "self_testing", 100,
                           "not_confirmed_until_health_check_passes");
    else if (s_boot.id[0])
    {
        const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
        esp_app_desc_t app;
        bool rolled = invalid && esp_ota_get_partition_description(invalid, &app) == ESP_OK &&
                      !strncmp(app.version, s_boot.version, sizeof(app.version));
        publish_ota_status(s_boot.id, s_boot.version, rolled ? "rolled_back" : "interrupted", 0,
                           rolled ? "previous_working_firmware_restored"
                                  : "previous_update_not_confirmed");
        (void)save_boot_record(NULL);
    }
    else
        publish_ota_status("", esp_app_get_description()->version, "idle", 0,
                           "ready_for_HTTPS_OTA");
    s_queue = xQueueCreate(1, sizeof(OtaRequest));
    ESP_ERROR_CHECK(s_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(ota_worker_task, "mouse_ota", 8192, NULL, 2, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
