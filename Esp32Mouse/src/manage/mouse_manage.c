#include "mouse_manage.h"
#include "mouse_ota.h"
#include "uart/uart_link.h"
#include "ble/ble_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stddef.h>

/* 管理通路，不是蓝牙鼠标报告通路。
 *
 * 阅读顺序：MouseManage_Start -> 各事件回调 -> management_task。
 * 回调只接收/校验/投递；管理任务负责等待ZE回执、重试联网和周期上报。
 * DPI寄存器由ZE的Input任务独占访问，这里只转发命令，不假装硬件已生效。
 */
static const char *TAG = "MANAGE";
/* allow_legacy_wpa占用旧布局的1字节填充，port仍在偏移228，总长230。
 * 默认0要求WPA2；只有明确配置的旧路由器才允许WPA1，绝不允许OPEN/WEP。 */
typedef struct
{
    char ssid[33];            /* 2.4GHz路由器名称 */
    char password[65];        /* Wi-Fi密码：只进NVS，不写日志 */
    char host[64];            /* 电脑Broker的局域网地址，不是localhost */
    char token[65];           /* 管理端生成的认证令牌 */
    uint8_t allow_legacy_wpa; /* 显式兼容旧路由器；默认仍要求WPA2 */
    uint16_t port;            /* MQTT TCP端口 */
} MouseNetworkConfig;
_Static_assert(sizeof(MouseNetworkConfig) == 230 && offsetof(MouseNetworkConfig, port) == 228,
               "Preserve existing NVS config layout");
typedef struct
{
    char id[49];  /* 网页业务请求ID，不是MQTT包标识符或UART序号 */
    char op[16];  /* get_state / set_dpi / ota_start */
    uint16_t dpi; /* 仅set_dpi使用 */
} MouseCommand;
static MouseNetworkConfig s_network_config;
static QueueHandle_t s_commands; /* MQTT回调 -> 管理任务：用户意图 */
static QueueHandle_t s_results;  /* UART收帧 -> 管理任务：ZE实际结果 */
static esp_mqtt_client_handle_t s_mqtt_client;
static volatile bool s_mqtt_connected, s_wifi_connected;
static bool s_mqtt_started;
static char s_topic_prefix[80];
static char s_topic_command[96];
static char s_topic_result[96];
static char s_topic_state[96];
static char s_topic_availability[96];
static char s_topic_ota[96];
static char s_station_ip[20] = "未连接";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_last_ze_ms;
static uint16_t s_actual_dpi;
static uint8_t s_actual_mode, s_actual_reg;
static bool s_actual_valid;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* MQTT发送与业务回执 ------------------------------------------------------ */
static void mqtt_publish(const char *topic, const char *json, bool retained)
{
    if (s_mqtt_connected)
        esp_mqtt_client_enqueue(s_mqtt_client, topic, json, 0, 1, retained, true);
}
static void publish_ota_status(const char *json)
{
    mqtt_publish(s_topic_ota, json, true);
}
static void publish_command_result(const MouseCommand *command, const char *status,
                                   const char *detail, const uint8_t *result)
{
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return;
    cJSON_AddStringToObject(j, "id", command->id);
    cJSON_AddStringToObject(j, "op", command->op);
    cJSON_AddStringToObject(j, "status", status);
    cJSON_AddStringToObject(j, "detail", detail);
    cJSON_AddNumberToObject(j, "requested_dpi", command->dpi);
    portENTER_CRITICAL(&s_state_lock);
    uint16_t dpi = s_actual_dpi;
    uint8_t reg = s_actual_reg, mode = s_actual_mode;
    bool fresh = s_actual_valid && (uint32_t)(uptime_ms() - s_last_ze_ms) < 3000;
    portEXIT_CRITICAL(&s_state_lock);
    /* 命令回执必须使用对应序号那一份读回值，不混入随后物理按键的新状态。 */
    if (result)
    {
        dpi = (uint16_t)(result[6] | ((uint16_t)result[7] << 8));
        reg = result[9];
        mode = result[10];
        fresh = true;
    }
    if (fresh)
    {
        cJSON_AddNumberToObject(j, "dpi", dpi);
        cJSON_AddNumberToObject(j, "register", reg);
        cJSON_AddNumberToObject(j, "mode", mode);
    }
    char *s = cJSON_PrintUnformatted(j);
    if (s)
    {
        mqtt_publish(s_topic_result, s, false);
        cJSON_free(s);
    }
    cJSON_Delete(j);
}
void MouseManage_Result(const uint8_t *p, size_t len)
{
    if (len != 12 || p[0] != 0xC2 || p[11] != 1)
        return;
    static bool reported;
    static uint16_t previous_dpi;
    uint16_t dpi = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));
    portENTER_CRITICAL(&s_state_lock);
    s_actual_dpi = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));
    s_actual_mode = p[10];
    s_actual_reg = p[9];
    s_last_ze_ms = uptime_ms();
    s_actual_valid = true;
    portEXIT_CRITICAL(&s_state_lock);
    /* 只打印首次状态、DPI变化和命令结果，不逐帧刷日志影响鼠标。 */
    if (!reported || dpi != previous_dpi || p[2] || p[3] || p[4] || p[5])
        ESP_LOGI(TAG, "ZE readback: dpi=%u Config1=0x%02x status=%u mode=%u", dpi, p[9], p[8],
                 p[10]);
    reported = true;
    previous_dpi = dpi;
    if (s_results)
        (void)xQueueSend(s_results, p, 0);
}

/* 一次MQTT消息可分成多个DATA事件，必须按偏移重组，不把TCP片段当完整JSON。 */
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t e = data;
    static char incoming[640];
    static int used, total;
    static bool accept;
    if (id == MQTT_EVENT_CONNECTED)
    {
        s_mqtt_connected = true;
        esp_mqtt_client_subscribe(s_mqtt_client, s_topic_command, 1);
        mqtt_publish(s_topic_availability, "{\"online\":true}", true);
        MouseOTA_PublishStatus();
        ESP_LOGI(TAG, "MQTT connected; commands=%s", s_topic_command);
    }
    else if (id == MQTT_EVENT_DISCONNECTED)
    {
        s_mqtt_connected = false;
        accept = false;
    }
    else if (id == MQTT_EVENT_DATA)
    {
        if (e->current_data_offset == 0)
        {
            used = 0;
            total = e->total_data_len;
            accept = total > 0 && total < (int)sizeof(incoming) && !e->retain &&
                     e->topic_len == (int)strlen(s_topic_command) &&
                     memcmp(e->topic, s_topic_command, e->topic_len) == 0;
        }
        if (!accept || e->current_data_offset != used || e->data_len < 0 ||
            used + e->data_len > total)
        {
            accept = false;
            return;
        }
        memcpy(incoming + used, e->data, e->data_len);
        used += e->data_len;
        if (used != total)
            return;
        incoming[used] = 0;
        accept = false;
        cJSON *j = cJSON_ParseWithLength(incoming, (size_t)used + 1);
        if (!j)
            return;
        cJSON *rid = cJSON_GetObjectItemCaseSensitive(j, "id"),
              *op = cJSON_GetObjectItemCaseSensitive(j, "op"),
              *dpi = cJSON_GetObjectItemCaseSensitive(j, "dpi");
        MouseCommand command = {0};
        if (cJSON_IsString(rid) && strlen(rid->valuestring) > 0 &&
            strlen(rid->valuestring) < sizeof(command.id) && cJSON_IsString(op) &&
            strlen(op->valuestring) < sizeof(command.op))
        {
            strcpy(command.id, rid->valuestring);
            strcpy(command.op, op->valuestring);
            if (strcmp(command.op, "ota_start") == 0)
            {
                cJSON *url = cJSON_GetObjectItemCaseSensitive(j, "url"),
                      *version = cJSON_GetObjectItemCaseSensitive(j, "version");
                cJSON *sha = cJSON_GetObjectItemCaseSensitive(j, "sha256"),
                      *size = cJSON_GetObjectItemCaseSensitive(j, "size");
                esp_err_t err = ESP_ERR_INVALID_ARG;
                if (cJSON_IsString(url) && cJSON_IsString(version) && cJSON_IsString(sha) &&
                    cJSON_IsNumber(size) && size->valuedouble == size->valueint &&
                    size->valueint > 0)
                    err = MouseOTA_Submit(command.id, url->valuestring, version->valuestring,
                                          sha->valuestring, (uint32_t)size->valueint);
                /* 成功后由OTA独立状态主题报告进度，不能伪装成ZE的applied。 */
                if (err != ESP_OK)
                    publish_command_result(&command, "rejected", esp_err_to_name(err), NULL);
                cJSON_Delete(j);
                return;
            }
            bool valid = strcmp(command.op, "get_state") == 0;
            if (strcmp(command.op, "set_dpi") == 0 && cJSON_IsNumber(dpi) &&
                (dpi->valuedouble == 800 || dpi->valuedouble == 1600 || dpi->valuedouble == 3200 ||
                 dpi->valuedouble == 6400))
            {
                command.dpi = (uint16_t)dpi->valueint;
                valid = true;
            }
            if (!valid)
                publish_command_result(&command, "rejected", "invalid_operation_or_dpi", NULL);
            else if (xQueueSend(s_commands, &command, 0) != pdPASS)
                publish_command_result(&command, "busy", "command_queue_full", NULL);
        }
        cJSON_Delete(j);
    }
}

/* Wi-Fi回调只更新状态。重连由管理任务定时发起，不在回调里死等。 */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        if (s_network_config.ssid[0])
            esp_wifi_connect();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_wifi_connected = false;
        wifi_event_sta_disconnected_t *e = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u", (unsigned)e->reason);
        /* 交给维护任务定时重试，避免回调里忙循环。 */
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = data;
        snprintf(s_station_ip, sizeof(s_station_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi IP=%s", s_station_ip);
    }
}

/* 首次配网网页：正常STA运行时不会开放这个无鉴权配置接口。 */
static const char portal[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport "
    "content='width=device-width'><title>鼠标配网</title>"
    "<style>body{font:18px sans-serif;max-width:560px;margin:40px "
    "auto;padding:20px;background:#eef3fa}input,button{box-sizing:border-box;width:100%;padding:"
    "12px;margin:8px 0}button{background:#225fd6;color:white;border:0}</style>"
    "<h1>鼠标网络配置</h1><p>连接2.4GHz "
    "Wi-Fi。Broker地址填写运行上位机的电脑局域网IP，不能填localhost。</p>"
    "<form id=f><label>Wi-Fi名称<input name=ssid maxlength=32 "
    "required></label><label>Wi-Fi密码<input name=password type=password maxlength=64></label>"
    "<label>电脑IP<input name=host maxlength=63 required "
    "placeholder='192.168.10.82'></label><label>MQTT端口<input name=port type=number "
    "value=18830></label>"
    "<label>上位机生成的连接令牌<input name=token type=password maxlength=64 "
    "required></label><button>保存，重启ESP32后连接</button></form><p id=msg></p>"
    "<script>f.onsubmit=async e=>{e.preventDefault();let v=Object.fromEntries(new "
    "FormData(f));v.port=Number(v.port);let r=await "
    "fetch('/config',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify(v)});msg.textContent=await r.text()};</script>";

static esp_err_t network_portal_get(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, portal, HTTPD_RESP_USE_STRLEN);
}
/* HTTP和串口维护共用校验与NVS入口；密码只经输入传入，不回显、不写日志。 */
static esp_err_t save_network_config(const char *body)
{
    cJSON *j = cJSON_Parse(body);
    if (!j)
        return ESP_ERR_INVALID_ARG;
    MouseNetworkConfig next = {0};
    const char *keys[] = {"ssid", "password", "host", "token"};
    char *dest[] = {next.ssid, next.password, next.host, next.token};
    size_t sizes[] = {33, 65, 64, 65};
    bool valid = true;
    for (int i = 0; i < 4; i++)
    {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(j, keys[i]);
        if (!cJSON_IsString(v) || strlen(v->valuestring) >= sizes[i] ||
            (i != 1 && !v->valuestring[0]))
        {
            valid = false;
            break;
        }
        strcpy(dest[i], v->valuestring);
    }
    cJSON *port = cJSON_GetObjectItemCaseSensitive(j, "port");
    cJSON *legacy = cJSON_GetObjectItemCaseSensitive(j, "allow_legacy_wpa");
    if (legacy && !cJSON_IsBool(legacy))
        valid = false;
    next.allow_legacy_wpa = cJSON_IsTrue(legacy) ? 1 : 0;
    if (!cJSON_IsNumber(port) || port->valuedouble != port->valueint || port->valueint < 1 ||
        port->valueint > 65535)
        valid = false;
    else
        next.port = (uint16_t)port->valueint;
    for (const char *h = next.host; *h; h++)
        if (!((*h >= '0' && *h <= '9') || (*h >= 'a' && *h <= 'z') || (*h >= 'A' && *h <= 'Z') ||
              *h == '.' || *h == '-'))
            valid = false;
    cJSON_Delete(j);
    if (!valid)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t n;
    esp_err_t err = nvs_open("mouse_net", NVS_READWRITE, &n);
    if (err == ESP_OK)
    {
        err = nvs_set_blob(n, "config", &next, sizeof(next));
        if (err == ESP_OK)
            err = nvs_commit(n);
        nvs_close(n);
    }
    return err;
}

static esp_err_t network_config_post(httpd_req_t *r)
{
    char body[512];
    if (r->content_len <= 0 || r->content_len >= (int)sizeof(body))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "body too large");
    int used = 0;
    while (used < r->content_len)
    {
        int n = httpd_req_recv(r, body + used, r->content_len - used);
        if (n <= 0)
            return ESP_FAIL;
        used += n;
    }
    body[used] = 0;
    esp_err_t err = save_network_config(body);
    memset(body, 0, sizeof(body));
    if (err == ESP_ERR_INVALID_ARG)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "invalid configuration");
    if (err != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    return httpd_resp_sendstr(r, "已保存。请按ESP32复位键，随后让电脑回到相同局域网。");
}

/* UART0是板上USB转串口的维护口，不是连接ZE的UART1。
 * 已配置但连不上Wi-Fi时，仍能通过物理串口重配，不必擦NVS/蓝牙绑定。
 * 每轮有界读取，不在回调里等输入；超长行整行丢弃，避免截断后误执行。
 */
static void poll_network_console(void)
{
    static char line[512];
    static size_t used;
    static bool overflow;
    uint8_t bytes[64];
    int count = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), 0);
    for (int i = 0; i < count; i++)
    {
        uint8_t c = bytes[i];
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            line[used] = 0;
            if (!overflow && strncmp(line, "NETCFG ", 7) == 0)
            {
                esp_err_t err = save_network_config(line + 7);
                memset(line, 0, sizeof(line));
                if (err == ESP_OK)
                {
                    ESP_LOGI(TAG, "NETCFG saved; restarting (credentials hidden)");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                }
                else
                    ESP_LOGW(TAG, "NETCFG rejected: %s", esp_err_to_name(err));
            }
            used = 0;
            overflow = false;
        }
        else if (!overflow)
        {
            if (used < sizeof(line) - 1)
                line[used++] = (char)c;
            else
            {
                overflow = true;
                memset(line, 0, sizeof(line));
                used = 0;
            }
        }
    }
    memset(bytes, 0, sizeof(bytes));
}

/* UART管理帧共16字节：AA55 + 长度12 + C1载荷 + 累加校验。
 * sequence关联ZE回执，command->id关联网页请求，两者不能混为同一个ID。 */
static void send_ze_command(uint32_t sequence, const MouseCommand *command)
{
    uint8_t f[16] = {0xAA, 0x55, 12, 0xC1};
    f[4] = strcmp(command->op, "set_dpi") == 0 ? 2 : 1;
    for (int i = 0; i < 4; i++)
        f[5 + i] = (uint8_t)(sequence >> (8 * i));
    f[9] = (uint8_t)command->dpi;
    f[10] = (uint8_t)(command->dpi >> 8);
    f[14] = 1;
    for (int i = 3; i < 15; i++)
        f[15] = (uint8_t)(f[15] + f[i]);
    (void)LinkUart_Write(f, sizeof(f));
}

/* 管理任务：一条DPI命令在途，其余明确返回busy；不在这里下载OTA文件。
 * 最近结果缓存只覆盖当前进程的有限重试，不宣称跨重启Exactly Once。 */
static void management_task(void *arg)
{
    (void)arg;
    MouseCommand pending = {0}, cached = {0};
    bool waiting = false, have_cache = false;
    char cached_status[16] = "", cached_detail[48] = "";
    uint8_t cached_result[12] = {0};
    uint32_t seq = (uint32_t)esp_timer_get_time(), deadline = 0, last_tele = 0, last_retry = 0;
    for (;;)
    {
        uint32_t now = uptime_ms();
        poll_network_console();
        if (s_wifi_connected && !s_mqtt_started)
        {
            char uri[100];
            snprintf(uri, sizeof(uri), "mqtt://%s:%u", s_network_config.host,
                     (unsigned)s_network_config.port);
            esp_mqtt_client_config_t cfg = {.broker.address.uri = uri,
                                            .credentials.username = "mouse",
                                            .credentials.authentication.password =
                                                s_network_config.token,
                                            .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
                                            .session.keepalive = 20,
                                            .session.last_will.topic = s_topic_availability,
                                            .session.last_will.msg = "{\"online\":false}",
                                            .session.last_will.qos = 1,
                                            .session.last_will.retain = true};
            s_mqtt_client = esp_mqtt_client_init(&cfg);
            if (s_mqtt_client)
            {
                esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                               NULL);
                esp_mqtt_client_start(s_mqtt_client);
                s_mqtt_started = true;
            }
        }
        if (!s_wifi_connected && s_network_config.ssid[0] && (uint32_t)(now - last_retry) > 5000)
        {
            last_retry = now;
            esp_wifi_connect();
        }
        uint8_t result[12];
        while (xQueueReceive(s_results, result, 0) == pdPASS)
        {
            uint32_t rid = (uint32_t)result[2] | ((uint32_t)result[3] << 8) |
                           ((uint32_t)result[4] << 16) | ((uint32_t)result[5] << 24);
            if (waiting && rid == seq)
            {
                const char *status = result[8] == 0 ? "applied" : "failed";
                const char *detail =
                    result[8] == 0 ? "ZE_sensor_readback" : "ZE_rejected_or_readback_mismatch";
                publish_command_result(&pending, status, detail, result);
                cached = pending;
                memcpy(cached_result, result, 12);
                strcpy(cached_status, status);
                strcpy(cached_detail, detail);
                have_cache = true;
                waiting = false;
            }
        }
        if (waiting && (int32_t)(now - deadline) >= 0)
        {
            publish_command_result(&pending, "timeout", "ZE_result_not_received", NULL);
            waiting = false;
        }
        MouseCommand next;
        if (xQueueReceive(s_commands, &next, 0) == pdPASS)
        {
            if (waiting)
                publish_command_result(&next, "busy", "previous_command_pending", NULL);
            else if (have_cache && strcmp(next.id, cached.id) == 0)
            {
                if (next.dpi == cached.dpi && strcmp(next.op, cached.op) == 0)
                    publish_command_result(&next, cached_status, cached_detail, cached_result);
                else
                    publish_command_result(&next, "rejected", "id_reused_with_different_command",
                                           NULL);
            }
            else
            {
                pending = next;
                seq++;
                if (!seq)
                    seq++;
                waiting = true;
                deadline = now + 2000;
                send_ze_command(seq, &pending);
                publish_command_result(&pending, "accepted", "waiting_for_ZE", NULL);
            }
        }
        if (s_mqtt_connected && (uint32_t)(now - last_tele) >= 1000)
        {
            char out[512];
            last_tele = now;
            portENTER_CRITICAL(&s_state_lock);
            bool fresh = s_actual_valid && (uint32_t)(now - s_last_ze_ms) < 3000;
            uint16_t dpi = s_actual_dpi;
            uint8_t mode = s_actual_mode, reg = s_actual_reg;
            portEXIT_CRITICAL(&s_state_lock);
            uint32_t flash;
            const char *partition;
            bool busy;
            MouseOTA_Info(&flash, &partition, &busy);
            snprintf(out, sizeof(out),
                     "{\"ze_online\":%s,\"dpi\":%u,\"mode\":%u,\"register\":%u,\"ble_connected\":%"
                     "s,\"version\":\"%s\",\"ip\":\"%s\",\"uptime_ms\":%" PRIu32
                     ",\"ota_capable\":true,\"ota_busy\":%s,\"flash_bytes\":%" PRIu32
                     ",\"partition\":\"%s\"}",
                     fresh ? "true" : "false", dpi, mode, reg,
                     BLEHID_IsConnected() ? "true" : "false", esp_app_get_description()->version,
                     s_station_ip, now, busy ? "true" : "false", flash, partition);
            mqtt_publish(s_topic_state, out, true);
        }
        portENTER_CRITICAL(&s_state_lock);
        bool ze_healthy = s_actual_valid && (uint32_t)(now - s_last_ze_ms) < 3000;
        portEXIT_CRITICAL(&s_state_lock);
        MouseOTA_Health(s_wifi_connected && s_mqtt_connected && ze_healthy && BLEHID_IsReady());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void MouseManage_Start(void)
{
    /* 保留现有UART0日志输出，只增加无回显的RX缓冲。 */
    if (!uart_is_driver_installed(UART_NUM_0))
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    s_commands = xQueueCreate(4, sizeof(MouseCommand));
    s_results = xQueueCreate(8, 12);
    ESP_ERROR_CHECK(s_commands && s_results ? ESP_OK : ESP_ERR_NO_MEM);
    nvs_handle_t n;
    size_t size = sizeof(s_network_config);
    if (nvs_open("mouse_net", NVS_READONLY, &n) == ESP_OK)
    {
        if (nvs_get_blob(n, "config", &s_network_config, &size) != ESP_OK ||
            size != sizeof(s_network_config))
            memset(&s_network_config, 0, sizeof(s_network_config));
        nvs_close(n);
    }
    s_network_config.ssid[32] = 0;
    s_network_config.password[64] = 0;
    s_network_config.host[63] = 0;
    s_network_config.token[64] = 0;
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t loop = esp_event_loop_create_default();
    ESP_ERROR_CHECK(loop == ESP_ERR_INVALID_STATE ? ESP_OK : loop);
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    snprintf(s_topic_prefix, sizeof(s_topic_prefix), "mouse/%02x%02x%02x%02x%02x%02x", mac[0],
             mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_topic_command, sizeof(s_topic_command), "%s/command", s_topic_prefix);
    snprintf(s_topic_result, sizeof(s_topic_result), "%s/result", s_topic_prefix);
    snprintf(s_topic_state, sizeof(s_topic_state), "%s/state", s_topic_prefix);
    snprintf(s_topic_availability, sizeof(s_topic_availability), "%s/availability", s_topic_prefix);
    snprintf(s_topic_ota, sizeof(s_topic_ota), "%s/ota/status", s_topic_prefix);
    MouseOTA_Init(s_network_config.host, s_network_config.token, publish_ota_status);
    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    wifi_config_t sta = {0}, ap = {0};
    memcpy(sta.sta.ssid, s_network_config.ssid, strlen(s_network_config.ssid));
    memcpy(sta.sta.password, s_network_config.password, strlen(s_network_config.password));
    sta.sta.threshold.authmode =
        s_network_config.allow_legacy_wpa == 1 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_WPA2_PSK;
    if (s_network_config.allow_legacy_wpa == 1)
        ESP_LOGW(TAG, "Legacy WPA compatibility explicitly enabled for configured network; "
                      "WPA2/WPA3 recommended");
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "MouseSetup-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    strcpy((char *)ap.ap.password, "MouseSetup24");
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 2;
    ESP_ERROR_CHECK(esp_wifi_set_mode(s_network_config.ssid[0] ? WIFI_MODE_STA : WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    if (!s_network_config.ssid[0])
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 初次配网才开放HTTP写配置；正常STA运行不暴露无认证配置接口。
     * 这是局域网学习版，尚未加入HTTPS/设备证书，不用于公网部署。 */
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 6144;
    httpd_handle_t server;
    if (!s_network_config.ssid[0] && httpd_start(&server, &http) == ESP_OK)
    {
        httpd_uri_t a = {.uri = "/", .method = HTTP_GET, .handler = network_portal_get};
        httpd_uri_t b = {.uri = "/config", .method = HTTP_POST, .handler = network_config_post};
        httpd_register_uri_handler(server, &a);
        httpd_register_uri_handler(server, &b);
    }
    ESP_LOGI(TAG, "Device %s; setup AP=%s (only when unconfigured), portal http://192.168.4.1",
             s_topic_prefix, (char *)ap.ap.ssid);
    ESP_ERROR_CHECK(xTaskCreate(management_task, "manage", 6144, NULL, 3, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
