#include "ble_hid.h"

#include <string.h>
#include <inttypes.h>
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatt_common_api.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "esp_hid_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static const char *TAG = "BLE_HID";

static esp_hidd_dev_t *s_hid_dev = NULL; // BLE 设备
static volatile bool s_hid_ready;
static portMUX_TYPE s_diag_lock = portMUX_INITIALIZER_UNLOCKED;
static ble_hid_diag_t s_diag = {.last_auth_result = -1, .mtu = 23};

/* ===== 保留配对记录重连时的订阅恢复 =====
 * ESP-IDF的HID库把报告CCCD值只放在RAM里；ESP32重启后为0。
 * 已绑定的Windows相信自己早已订阅，不会重写CCCD，于是库拒绝发送。
 * 这里保存“对端地址+CCCD句柄+最近一次真实写入值”，连接后原样恢复。
 */
#define CCC_NS "ble_ccc"
#define CCC_KEY "entries"
#define CCC_MAX 8

typedef struct
{
    uint8_t bda[6];
    uint16_t handle;
    uint16_t value;
    uint16_t layout; /* HID服务起始句柄；固件布局变化时让旧记录失效 */
} ccc_entry_t;

static ccc_entry_t s_ccc[CCC_MAX];
static size_t s_ccc_count;
static bool s_ccc_ready;
static uint16_t s_ccc_layout;
static uint16_t s_input_cccd_handle;
static esp_gatt_if_t s_gatts_if;
static uint16_t s_conn_id;

static void ble_ccc_save(void)
{
    nvs_handle_t h;
    if (nvs_open(CCC_NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    if (nvs_set_blob(h, CCC_KEY, s_ccc, s_ccc_count * sizeof(ccc_entry_t)) == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
}

static void ble_ccc_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_ccc);
    if (nvs_open(CCC_NS, NVS_READONLY, &h) == ESP_OK)
    {
        if (nvs_get_blob(h, CCC_KEY, s_ccc, &len) == ESP_OK && len % sizeof(ccc_entry_t) == 0)
        {
            s_ccc_count = len / sizeof(ccc_entry_t);
            if (s_ccc_count > CCC_MAX)
                s_ccc_count = CCC_MAX;
        }
        nvs_close(h);
    }
    s_ccc_ready = true;
}

static void ble_ccc_record(const uint8_t bda[6], uint16_t handle, uint16_t value)
{
    if (!s_ccc_ready)
        return;
    for (size_t i = 0; i < s_ccc_count; ++i)
    {
        if (s_ccc[i].handle == handle && memcmp(s_ccc[i].bda, bda, 6) == 0)
        {
            if (s_ccc[i].value == value && s_ccc[i].layout == s_ccc_layout)
                return;
            s_ccc[i].value = value;
            s_ccc[i].layout = s_ccc_layout;
            ble_ccc_save();
            return;
        }
    }
    if (s_ccc_count >= CCC_MAX)
        return;
    memcpy(s_ccc[s_ccc_count].bda, bda, 6);
    s_ccc[s_ccc_count].handle = handle;
    s_ccc[s_ccc_count].value = value;
    s_ccc[s_ccc_count].layout = s_ccc_layout;
    s_ccc_count++;
    ble_ccc_save();
}

/* 以一次“对端写入CCCD”的形式交给HID库，让它内部的订阅状态与真实主机一致。 */
static void ble_ccc_inject(const uint8_t bda[6], uint16_t handle, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    esp_ble_gatts_cb_param_t p;
    if (s_gatts_if == 0 || handle == 0)
        return;
    memset(&p, 0, sizeof(p));
    p.write.conn_id = s_conn_id;
    memcpy(p.write.bda, bda, 6);
    p.write.handle = handle;
    p.write.len = 2;
    p.write.value = bytes;
    p.write.need_rsp = false;
    ESP_LOGI(TAG, "CCC restore handle=0x%04x value=0x%04x", (unsigned)handle, (unsigned)value);
    esp_hidd_gatts_event_handler(ESP_GATTS_WRITE_EVT, s_gatts_if, &p);
    s_diag.ccc_replays++;
}

/* 安全完成后恢复：优先用该对端真实保存的值；没有记录时按已绑定HID鼠标补开订阅。 */
static void ble_ccc_restore(const uint8_t bda[6])
{
    bool restored = false;
    for (size_t i = 0; i < s_ccc_count; ++i)
    {
        if (s_ccc[i].layout != s_ccc_layout)
            continue;
        if (memcmp(s_ccc[i].bda, bda, 6) != 0)
            continue;
        ble_ccc_inject(bda, s_ccc[i].handle, s_ccc[i].value);
        restored = true;
    }
    if (!restored && s_input_cccd_handle != 0)
        ble_ccc_inject(bda, s_input_cccd_handle, 1);
}

/* 先记录少量关键事件，再原样交给HID库。
 * 这里不能漏转发或自行替库应答，否则诊断本身会破坏正常协议流程。 */
static void ble_gatts_diag_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t *param)
{
    if (gatts_if != 0)
        s_gatts_if = gatts_if;
    if (param != NULL)
    {
        switch (event)
        {
        case ESP_GATTS_WRITE_EVT:
        {
            uint16_t value = 0;
            if (param->write.value != NULL && param->write.len > 0)
                value = param->write.value[0];
            if (param->write.value != NULL && param->write.len > 1)
                value |= (uint16_t)param->write.value[1] << 8;
            portENTER_CRITICAL(&s_diag_lock);
            s_diag.write_events++;
            s_diag.last_write_handle = param->write.handle;
            s_diag.last_write_length = param->write.len;
            s_diag.last_write_value = value;
            portEXIT_CRITICAL(&s_diag_lock);
            ESP_LOGI(TAG, "GATT WRITE handle=0x%04x len=%u first_le16=0x%04x",
                     (unsigned)param->write.handle, (unsigned)param->write.len, (unsigned)value);
            /* 只把两字节、值0~2的写入当作CCCD；不要误记控制点或协议模式。 */
            if (param->write.len == 2 && param->write.value != NULL && param->write.value[1] == 0 &&
                param->write.value[0] <= 2)
            {
                ble_ccc_record(param->write.bda, param->write.handle, value);
                s_diag.ccc_records++;
            }
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            /* 按服务UUID定位HID表，避免与电池、设备信息服务混淆。
             * 单个输入报告在REPORT模式下的表尾顺序为：
             *   字符声明、报告值、CCCD、报告引用描述符。
             * 因此CCCD是倒数第2项。 */
            ESP_LOGI(TAG, "attr table uuid_len=%u uuid16=0x%04x handles=%u",
                     (unsigned)param->add_attr_tab.svc_uuid.len,
                     (unsigned)param->add_attr_tab.svc_uuid.uuid.uuid16,
                     (unsigned)param->add_attr_tab.num_handle);
            /* 电池与设备信息服务各5条；本工程HID服务为18条。
             * 第一个报告是6字节输入报告，其CCCD固定在下标13。 */
            if (param->add_attr_tab.handles != NULL && param->add_attr_tab.num_handle >= 14 &&
                param->add_attr_tab.svc_uuid.len == ESP_UUID_LEN_16 &&
                param->add_attr_tab.svc_uuid.uuid.uuid16 == ESP_GATT_UUID_HID_SVC)
            {
                s_ccc_layout = param->add_attr_tab.handles[0];
                s_input_cccd_handle = param->add_attr_tab.handles[13];
                s_diag.cccd_handle = s_input_cccd_handle;
                ESP_LOGI(TAG, "input report CCCD handle=0x%04x", (unsigned)s_input_cccd_handle);
            }
            break;
        case ESP_GATTS_CONNECT_EVT:
            s_conn_id = param->connect.conn_id;
            break;
        case ESP_GATTS_CONF_EVT:
            portENTER_CRITICAL(&s_diag_lock);
            s_diag.complete_events++;
            s_diag.last_complete_status = (uint16_t)param->conf.status;
            if (param->conf.status != ESP_GATT_OK)
                s_diag.complete_failures++;
            portEXIT_CRITICAL(&s_diag_lock);
            break;
        case ESP_GATTS_MTU_EVT:
            portENTER_CRITICAL(&s_diag_lock);
            s_diag.mtu = param->mtu.mtu;
            portEXIT_CRITICAL(&s_diag_lock);
            ESP_LOGI(TAG, "GATT MTU=%u", (unsigned)param->mtu.mtu);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "GATT disconnect reason=0x%x", (unsigned)param->disconnect.reason);
            break;
        default:
            break;
        }
    }
    esp_hidd_gatts_event_handler(event, gatts_if, param);
}

/* 报告描述符：与 STM32 USB HID 完全一致（HID 1.1 标准，USB/BLE 通用）
 * 结构：4 按键位 + 4 填充位 + X16 + Y16 + 滚轮8 = 6 字节报告 */
static const uint8_t s_report_map[62] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x04,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x04, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x04, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
};

/* ============ GAP 层：广播 + 配对/安全 ============ */

/* GAP 回调：广播结果、配对应答都走这里。
 * 主机连上后请求加密/配对，会触发 SEC_REQ，必须回一个"同意"，
 * 否则配对会一直卡住。这就是之前缺的"确认包"。 */
static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: // 广播结果
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
            ESP_LOGI(TAG, "advertising started");
        else
            ESP_LOGE(TAG, "advertising failed, status=0x%x",
                     (unsigned)param->adv_start_cmpl.status);
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        /* 主机请求配对加密 → 同意（true） */
        ESP_LOGI(TAG, "BLE SEC_REQ, accepting pairing");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true); // 配对加密
        break;

    case ESP_GAP_BLE_NC_REQ_EVT:
        /* 配对码确认（6位数字）→ 自动同意 */
        ESP_LOGI(TAG, "BLE NC_REQ passkey:%" PRIu32, param->ble_security.key_notif.passkey);
        esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
        break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: // 配对码
        ESP_LOGI(TAG, "BLE PASSKEY_NOTIF passkey:%" PRIu32, param->ble_security.key_notif.passkey);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT: // 配对完成
        /* 绑定重连时也可能收到安全完成事件；不等于主机已开启报告订阅。 */
        portENTER_CRITICAL(&s_diag_lock);
        s_diag.auth_events++;
        if (!param->ble_security.auth_cmpl.success)
            s_diag.auth_failures++;
        s_diag.last_auth_result =
            param->ble_security.auth_cmpl.success ? 0 : param->ble_security.auth_cmpl.fail_reason;
        portEXIT_CRITICAL(&s_diag_lock);
        if (param->ble_security.auth_cmpl.success) // 配对成功
        {
            ESP_LOGI(TAG, "BLE pairing OK");
            /* 绑定重连时Windows不会重写CCCD，需要在这里把库内状态补回来。 */
            ble_ccc_restore(param->ble_security.auth_cmpl.bd_addr);
        }
        else
        {
            ESP_LOGE(TAG, "BLE pairing failed: 0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        break;

    default:
        break;
    }
}

/* 广播+安全参数初始化：设备名、外观、配对方式、广播内容 */
static esp_err_t ble_hid_adv_init(uint16_t appearance, const char *device_name)
{
    /* 手工拼广播包（市售 BLE 鼠标同款）：
     * flags + 设备名 + 发射功率 + 外观(鼠标) + 16位 HID 服务 UUID(0x1812)
     * Windows 靠广播里的 16 位 HID UUID 把设备归类成 HID，
     * 之前放扫描响应里的 128 位形式可能不被识别 */
    uint8_t adv_raw[32];
    uint8_t idx = 0;

    /* Flags：LE 通用可发现 + 不支持 BR/EDR */
    adv_raw[idx++] = 0x02;
    adv_raw[idx++] = 0x01;
    adv_raw[idx++] = 0x06;

    /* 完整设备名 */
    uint8_t name_len = (uint8_t)strlen(device_name);
    adv_raw[idx++] = name_len + 1;
    adv_raw[idx++] = 0x09; /* Complete Local Name */
    memcpy(&adv_raw[idx], device_name, name_len);
    idx += name_len;

    /* 发射功率 */
    adv_raw[idx++] = 0x02;
    adv_raw[idx++] = 0x0A;
    adv_raw[idx++] = 0x0C;

    /* 外观 = 鼠标（小端 0x03C2） */
    adv_raw[idx++] = 0x03;
    adv_raw[idx++] = 0x19; /* Appearance */
    adv_raw[idx++] = (uint8_t)(appearance & 0xFF);
    adv_raw[idx++] = (uint8_t)(appearance >> 8);

    /* 16 位 HID 服务 UUID 0x1812（小端 12 18） */
    adv_raw[idx++] = 0x03;
    adv_raw[idx++] = 0x03; /* 16-bit Service UUID */
    adv_raw[idx++] = (uint8_t)(ESP_GATT_UUID_HID_SVC & 0xFF);
    adv_raw[idx++] = (uint8_t)(ESP_GATT_UUID_HID_SVC >> 8);

    /* 安全参数：Just Works（免确认）——鼠标没有屏幕和键盘，
     * 不能用"数字核对"(MITM)。SC + BOND，去掉 MITM，IO 能力=NONE，
     * 配对时不再弹配对码，和市售 BLE 鼠标一致 */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND; /* 配对方式 */
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;              /* 无输入输出 → 免确认配对 */
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t key_size = 16; /* 密钥长度 16 字节 */
    uint32_t passkey = 1234;
    /* 配对参数初始化 */
    ESP_ERROR_CHECK(
        esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, 1));     // 配对方式
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, 1)); // IO 能力
    ESP_ERROR_CHECK(
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, 1)); // 初始密钥
    ESP_ERROR_CHECK(
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, 1)); // 响应密钥
    ESP_ERROR_CHECK(
        esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1)); // 最大密钥长度
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey,
                                                   sizeof(uint32_t))); // 配对码
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(device_name));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(adv_raw, idx));
    return ESP_OK;
}

/* 真正开始广播（可重复调用：断开后重新广播也用它） */
static esp_err_t ble_hid_adv_start(void)
{
    static esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20,      /* 广播间隔 40ms */
        .adv_int_max = 0x30,      /* 广播间隔 60ms */
        .adv_type = ADV_TYPE_IND, /* 可连接+可扫描 */
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL, /* 三个信道都发 */
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    return esp_ble_gap_start_advertising(&adv_params);
}

/* ============ HID 设备层事件 ============ */

/* 事件回调：库有事(连上/断开)就"打电话"执行这里 */
static void ble_hid_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                                  void *event_data)
{
    switch ((esp_hidd_event_t)event_id)
    {
    case ESP_HIDD_START_EVENT:
        s_hid_ready = true;
        /* 库准备好了 → 开始广播，让电脑能搜到 */
        ESP_LOGI(TAG, "HID device started, starting advertising...");
        if (ble_hid_adv_start() != ESP_OK)
        {
            ESP_LOGE(TAG, "advertising start failed");
        }
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "BLE host connected!");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        /* 断开 → 重新广播，让电脑能再搜到 */
        ESP_LOGI(TAG, "BLE host disconnected, re-advertising...");
        if (ble_hid_adv_start() != ESP_OK)
        {
            ESP_LOGE(TAG, "re-advertising failed");
        }
        break;
    default:
        break;
    }
}

void BLEHID_Init(void)
{
    /* 1. NVS：蓝牙需要存储配对信息 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ble_ccc_load();

    /* 2. 蓝牙控制器：只用 BLE，释放经典蓝牙占的内存 */
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    /* 控制器只编译了 BLE（sdkconfig 里没开经典蓝牙），
     * 所以使能模式必须用 ESP_BT_MODE_BLE（mode 1），
     * 用 BTDM(mode 3) 会报 invalid mode 3 */
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    /* 3. Bluedroid 协议栈 */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_LOGI(TAG, "stored bond devices=%d", esp_ble_get_bond_device_num());

    /* 4. GAP：注册回调（广播结果/配对应答）+ 配置广播与安全参数 */
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_event_handler));
    ESP_ERROR_CHECK(ble_hid_adv_init(ESP_HID_APPEARANCE_MOUSE, "ESP32 Mouse"));

    /* 5. GATTS：必须把库内置的 GATTS 回调注册给蓝牙协议栈，
     *    否则"服务启动"事件送不进去，START 事件永远不触发、广播起不来 */
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(ble_gatts_diag_handler));

    /* 6. HID 设备：名字 + 报告描述符 + 回调
     *    注册完成后会触发 ESP_HIDD_START_EVENT，在那里真正开广播 */
    esp_hid_device_config_t hid_cfg = {
        .vendor_id = 0x0101,  /* 和 USB 的 VID 一致 */
        .product_id = 0x3939, /* 和 USB 的 PID 一致 */
        .device_name = "ESP32 Mouse",
        .manufacturer_name = "ILEMIKU",
        .report_maps =
            (esp_hid_raw_report_map_t[]){
                {.data = s_report_map, .len = sizeof(s_report_map)},
            },
        .report_maps_len = 1,
    };

    ESP_ERROR_CHECK(
        esp_hidd_dev_init(&hid_cfg, ESP_HID_TRANSPORT_BLE, ble_hid_event_handler, &s_hid_dev));
    ESP_LOGI(TAG, "BLE HID ready, pair from PC...");
}

bool BLEHID_IsConnected(void)
{
    return (s_hid_dev != NULL) && esp_hidd_dev_connected(s_hid_dev);
}

bool BLEHID_IsReady(void)
{
    return s_hid_ready;
}

esp_err_t BLEHID_SendReport(const uint8_t report[6])
{
    if (!BLEHID_IsConnected())
    {
        /* 由每秒统计报告未连接次数，避免每帧刷屏干扰观察。 */
        return ESP_ERR_INVALID_STATE;
    }

    return esp_hidd_dev_input_set(s_hid_dev, 0, 0, (uint8_t *)report, 6);
}

void BLEHID_GetDiagnostics(ble_hid_diag_t *out)
{
    if (out == NULL)
        return;
    portENTER_CRITICAL(&s_diag_lock);
    *out = s_diag;
    portEXIT_CRITICAL(&s_diag_lock);
}
