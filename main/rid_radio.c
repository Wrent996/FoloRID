// main/rid_radio.c —— WiFi 混杂模式 + NimBLE Observer 双链路 RID 扫描
//
// 生命周期：
//   start: NVS/netif 准备 → WiFi NULL 模式混杂 → BLE host observer
//   stop : 先停回调与任务，再反初始化协议栈
// 解析在独立任务中执行，不占用 LVGL/按键上下文。
#include "rid_radio.h"
#include "rid_odid.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "rid_radio";

#define RID_WIFI_CHANNEL_MIN 1
#define RID_WIFI_CHANNEL_MAX 13
#define RID_WIFI_HOP_MS 400
#define RID_BLE_SCAN_WINDOW_MS 2000
#define RID_STORE_LOCK_MS 100

typedef struct {
    uint8_t mac[6];
    uint8_t frame[256];
    uint16_t len;
    int8_t rssi;
    bool valid;
} rid_radio_pkt_t;

static rid_store_t *s_store;
static SemaphoreHandle_t s_store_lock;
static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_parse_task;
static TaskHandle_t s_hop_task;
static volatile bool s_running;
static volatile bool s_paused;
static volatile int s_state;          // 0 stop 1 run 2 fail
static volatile int s_wifi_channel;
static bool s_wifi_init;
static bool s_wifi_started;
static bool s_ble_init;
static bool s_wifi_handler;

/* 环形包缓冲：WiFi 回调只 memcpy，解析在任务里做 */
#define RID_PKT_RING 8
static rid_radio_pkt_t s_ring[RID_PKT_RING];
static volatile uint32_t s_ring_w;
static rid_odid_result_t s_ble_result;
static uint8_t s_ble_mac[6];
static volatile bool s_ble_result_ready;

static void parse_task(void *arg);
static void hop_task(void *arg);
static int ble_gap_event(struct ble_gap_event *event, void *arg);

static void on_wifi_promiscuous(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!s_running || s_paused || !buf) {
        return;
    }
    // 仅处理管理帧（Beacon/Probe）与可能的 Action/NAN
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_MISC && type != WIFI_PKT_CTRL) {
        /* still try mgmt mainly */
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    // payload 为柔性数组成员，地址恒有效；仅校验长度
    if (pkt->rx_ctrl.sig_len <= 0 || pkt->rx_ctrl.sig_len > 250) {
        return;
    }
    uint32_t idx = s_ring_w % RID_PKT_RING;
    rid_radio_pkt_t *slot = &s_ring[idx];
    slot->rssi = pkt->rx_ctrl.rssi;
    slot->len = (uint16_t)pkt->rx_ctrl.sig_len;
    memcpy(slot->frame, pkt->payload, slot->len);
    // addr2 在 offset 10
    memcpy(slot->mac, &pkt->payload[10], 6);
    slot->valid = true;
    s_ring_w = idx + 1;
    if (s_parse_task) {
        xTaskNotifyGive(s_parse_task);
    }
}

static void process_wifi_frame(const rid_radio_pkt_t *pkt)
{
    if (!s_store || !pkt || !pkt->valid) {
        return;
    }
    rid_odid_result_t res;
    memset(&res, 0, sizeof(res));
    bool ok = rid_odid_parse_wifi_beacon(pkt->frame, pkt->len, &res);
    rid_link_t link = RID_LINK_WIFI_BEACON;
    if (!ok) {
        memset(&res, 0, sizeof(res));
        ok = rid_odid_parse_wifi_nan(pkt->frame, pkt->len, &res);
        link = RID_LINK_WIFI_NAN;
    }
    if (!ok) {
        return;
    }
    if (xSemaphoreTake(s_store_lock, pdMS_TO_TICKS(RID_STORE_LOCK_MS)) == pdTRUE) {
        rid_odid_apply_to_store(s_store, pkt->mac, &res, link, pkt->rssi,
                                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        xSemaphoreGive(s_store_lock);
    }
}

static void parse_task(void *arg)
{
    (void)arg;
    uint32_t last_w = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        if (!s_running) {
            break;
        }
        while (last_w != s_ring_w) {
            uint32_t idx = last_w % RID_PKT_RING;
            if (s_ring[idx].valid) {
                process_wifi_frame(&s_ring[idx]);
                s_ring[idx].valid = false;
            }
            last_w++;
        }
        if (s_ble_result_ready) {
            s_ble_result_ready = false;
            if (xSemaphoreTake(s_store_lock, pdMS_TO_TICKS(RID_STORE_LOCK_MS)) == pdTRUE) {
                rid_odid_apply_to_store(s_store, s_ble_mac, &s_ble_result, RID_LINK_BLE,
                                        -50, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
                xSemaphoreGive(s_store_lock);
            }
        }
    }
    vTaskDelete(NULL);
}

static void hop_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_running) {
            break;
        }
        if (!s_paused && s_wifi_started) {
            int ch = s_wifi_channel;
            if (ch < RID_WIFI_CHANNEL_MIN || ch > RID_WIFI_CHANNEL_MAX) {
                ch = RID_WIFI_CHANNEL_MIN;
            } else {
                ch++;
                if (ch > RID_WIFI_CHANNEL_MAX) {
                    ch = RID_WIFI_CHANNEL_MIN;
                }
            }
            esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
            s_wifi_channel = ch;
        }
        vTaskDelay(pdMS_TO_TICKS(RID_WIFI_HOP_MS));
    }
    vTaskDelete(NULL);
}

static void on_ble_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (!event || !s_running || s_paused) {
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &event->disc;
        if (d && d->length_data > 0 && d->data) {
            rid_odid_result_t res;
            memset(&res, 0, sizeof(res));
            if (rid_odid_parse_ble_adv(d->data, (size_t)d->length_data, &res)) {
                memcpy(s_ble_mac, d->addr.val, 6);
                s_ble_result = res;
                s_ble_result_ready = true;
                if (s_parse_task) {
                    xTaskNotifyGive(s_parse_task);
                }
            }
        }
    }
    return 0;
}

static void on_ble_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        // 持续扫描
        struct ble_gap_disc_params params = { 0 };
        params.passive = 1;
        params.filter_duplicates = 0;
        params.itvl = 0;
        params.window = 0;
        params.limited = 0;
        rc = ble_gap_disc(0, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE 扫描启动失败: %d", rc);
        s_state = 2;
    }
}

static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) {
        xSemaphoreGive(s_host_stopped);
    }
    nimble_port_freertos_deinit();
}

static esp_err_t wifi_start(void)
{
    esp_err_t err = esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        return err;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    s_wifi_init = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    s_wifi_started = true;
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&on_wifi_promiscuous);
    esp_wifi_set_channel(RID_WIFI_CHANNEL_MIN, WIFI_SECOND_CHAN_NONE);
    s_wifi_channel = RID_WIFI_CHANNEL_MIN;
    s_wifi_handler = true;
    return ESP_OK;
}

static void wifi_stop(void)
{
    if (s_wifi_started) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_init) {
        esp_wifi_deinit();
        s_wifi_init = false;
    }
    (void)s_wifi_handler;
    s_wifi_handler = false;
}

static esp_err_t ble_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    s_ble_init = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) {
        return ESP_ERR_NO_MEM;
    }
    ble_hs_cfg.reset_cb = on_ble_reset;
    ble_hs_cfg.sync_cb = on_ble_sync;
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

static void ble_stop(void)
{
    if (!s_ble_init) {
        return;
    }
    (void)ble_gap_disc_cancel();
    int rc = nimble_port_stop();
    if (rc == 0 && s_host_stopped) {
        (void)xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(2000));
    }
    (void)nimble_port_deinit();
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    s_ble_init = false;
}

esp_err_t rid_radio_start(rid_store_t *store)
{
    if (!store) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) {
        return ESP_OK;
    }
    s_store = store;
    s_paused = false;
    s_state = 1;
    s_ring_w = 0;
    s_ble_result_ready = false;
    memset(s_ring, 0, sizeof(s_ring));
    esp_err_t err = ESP_OK;

    if (!s_store_lock) {
        s_store_lock = xSemaphoreCreateMutex();
        if (!s_store_lock) {
            s_state = 2;
            return ESP_ERR_NO_MEM;
        }
    }

    /* NVS：Wi-Fi/BLE 协议栈依赖；失败时不自动擦除分区 */
    static bool s_nvs_ready;
    if (!s_nvs_ready) {
        esp_err_t nvs_err = nvs_flash_init();
        if (nvs_err != ESP_OK) {
            ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(nvs_err));
            err = nvs_err;
            goto fail;
        }
        s_nvs_ready = true;
    }

    static bool s_netif_ready;
    static bool s_event_ready;
    if (!s_netif_ready) {
        if (esp_netif_init() != ESP_OK) {
            err = ESP_FAIL;
            goto fail;
        }
        s_netif_ready = true;
    }
    if (!s_event_ready) {
        if (esp_event_loop_create_default() != ESP_OK) {
            err = ESP_FAIL;
            goto fail;
        }
        s_event_ready = true;
    }

    s_running = true;
    if (xTaskCreate(parse_task, "rid_parse", 4096, NULL, 5, &s_parse_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    if (xTaskCreate(hop_task, "rid_hop", 2048, NULL, 4, &s_hop_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    err = wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 扫描启动失败: %s", esp_err_to_name(err));
        // BLE 仍可独立工作
    }
    err = ble_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE 扫描启动失败: %s", esp_err_to_name(err));
    }
    if (!s_wifi_started && !s_ble_init) {
        goto fail;
    }
    ESP_LOGI(TAG, "RID 射频已启动 WiFi=%d BLE=%d", s_wifi_started, s_ble_init);
    return ESP_OK;

fail:
    (void)rid_radio_stop();
    s_state = 2;
    return err != ESP_OK ? err : ESP_FAIL;
}

esp_err_t rid_radio_stop(void)
{
    s_running = false;
    s_paused = false;
    ble_stop();
    wifi_stop();
    if (s_parse_task) {
        // parse_task 会在 s_running=false 后退出
        vTaskDelay(pdMS_TO_TICKS(50));
        s_parse_task = NULL;
    }
    s_hop_task = NULL;
    s_store = NULL;
    s_state = 0;
    return ESP_OK;
}

void rid_radio_set_paused(bool paused)
{
    s_paused = paused;
}

bool rid_radio_paused(void)
{
    return s_paused;
}

int rid_radio_state(void)
{
    return s_state;
}

int rid_radio_wifi_channel(void)
{
    return s_wifi_channel;
}

/** 应用层访问 store 锁（UI 刷新时）。 */
bool rid_radio_lock_store(int timeout_ms)
{
    if (!s_store_lock) {
        return true;
    }
    return xSemaphoreTake(s_store_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void rid_radio_unlock_store(void)
{
    if (s_store_lock) {
        xSemaphoreGive(s_store_lock);
    }
}
