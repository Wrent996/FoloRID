// main/main.c —— 开源 ESP32 Remote ID 接收器（FoloRID）
//
// 产品定位：基于 FoloToy AI Passport 的开源无人机远程识别接收端。
// 解析 OpenDroneID / ASTM F3411 WiFi Beacon、NAN 与 BLE 广播，
// 在 240×320 屏上以中文值守终端界面展示目标列表与详情。
//
// 按键：
//   UP/DOWN  列表选择
//   OK 单击  列表→详情 / 详情→返回；列表无目标时显示关于
//   OK 长按  列表中暂停/恢复扫描；详情中返回
//
// 与 demo 菜单完全分离：本文件即应用入口，不使用 ui_pixel 主题。
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "rid_odid.h"
#include "rid_radio.h"
#include "rid_radio_lock.h"
#include "rid_store.h"
#include "rid_ui.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <string.h>

static const char *TAG = "folo_rid";

#define INPUT_QUEUE_DEPTH 8
#define UI_REFRESH_MS 400
#define EXPIRE_CHECK_MS 2000

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static rid_store_t s_store;
static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_ready;
static int s_selected;               /**< 当前选中的 store 槽位索引 */
static lv_timer_t *s_ui_timer;
static uint32_t s_last_expire_ms;
static bool s_radio_ok;

/** 将 store 中活动索引列表里的 selection 对齐到槽位号。 */
static void clamp_selection(void)
{
    int idx[RID_STORE_MAX_UAVS];
    int n = 0;
    if (rid_radio_lock_store(100)) {
        n = rid_store_list_indices(&s_store, idx, RID_STORE_MAX_UAVS);
        rid_radio_unlock_store();
    }
    if (n <= 0) {
        s_selected = -1;
        return;
    }
    for (int i = 0; i < n; i++) {
        if (idx[i] == s_selected) {
            return;
        }
    }
    s_selected = idx[0];
}

static void move_selection(int delta)
{
    int idx[RID_STORE_MAX_UAVS];
    int n = 0;
    if (!rid_radio_lock_store(100)) {
        return;
    }
    n = rid_store_list_indices(&s_store, idx, RID_STORE_MAX_UAVS);
    rid_radio_unlock_store();
    if (n <= 0) {
        s_selected = -1;
        return;
    }
    int pos = 0;
    for (int i = 0; i < n; i++) {
        if (idx[i] == s_selected) {
            pos = i;
            break;
        }
    }
    pos += delta;
    if (pos < 0) {
        pos = n - 1;
    }
    if (pos >= n) {
        pos = 0;
    }
    s_selected = idx[pos];
}

static rid_ui_scan_state_t map_scan_state(void)
{
    if (rid_radio_paused()) {
        return RID_UI_SCAN_PAUSED;
    }
    int st = rid_radio_state();
    if (st == 1) {
        return RID_UI_SCAN_RUNNING;
    }
    if (st == 2) {
        return RID_UI_SCAN_FAILED;
    }
    return RID_UI_SCAN_OFF;
}

static void refresh_ui(void)
{
    if (!bsp_lvgl_lock(100)) {
        return;
    }
    clamp_selection();
    int count = 0;
    uint16_t wifi_hits = 0;
    uint16_t ble_hits = 0;
    if (rid_radio_lock_store(50)) {
        count = rid_store_active_count(&s_store);
        wifi_hits = s_store.wifi_hits;
        ble_hits = s_store.ble_hits;
        rid_ui_refresh(&s_store, s_selected, map_scan_state(),
                       (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        rid_radio_unlock_store();
    }
    rid_ui_set_status(map_scan_state(), count, wifi_hits, ble_hits, bsp_battery_soc());
    bsp_lvgl_unlock();
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    // 过期清理（射频任务写 store，这里在锁内做）
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - s_last_expire_ms) > EXPIRE_CHECK_MS) {
        s_last_expire_ms = now;
        if (rid_radio_lock_store(20)) {
            (void)rid_store_expire(&s_store, now, RID_STORE_EXPIRE_MS);
            rid_radio_unlock_store();
        }
    }
    refresh_ui();
}

static void show_about(void)
{
    if (!bsp_lvgl_lock(200)) {
        return;
    }
    rid_ui_set_page(RID_UI_PAGE_DETAIL);
    rid_ui_set_hint("OK/长按:返回列表");
    /* 详情页在无选中时会显示“无目标数据”；这里通过 hint 补充说明 */
    bsp_lvgl_unlock();
    /* 用状态行展示关于信息 */
    if (bsp_lvgl_lock(200)) {
        rid_ui_set_status(RID_UI_SCAN_RUNNING, 0, s_store.wifi_hits, s_store.ble_hits,
                          bsp_battery_soc());
        bsp_lvgl_unlock();
    }
    rid_ui_set_hint("开源RID接收器  OpenDroneID");
}

static void process_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_ready) {
        return;
    }
    rid_ui_page_t page = rid_ui_page();

    if (page == RID_UI_PAGE_LIST) {
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
            if (!bsp_lvgl_lock(100)) {
                return;
            }
            move_selection(-1);
            bsp_lvgl_unlock();
            refresh_ui();
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            if (!bsp_lvgl_lock(100)) {
                return;
            }
            move_selection(1);
            bsp_lvgl_unlock();
            refresh_ui();
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            clamp_selection();
            if (s_selected < 0) {
                show_about();
                return;
            }
            if (!bsp_lvgl_lock(100)) {
                return;
            }
            rid_ui_set_page(RID_UI_PAGE_DETAIL);
            bsp_lvgl_unlock();
            refresh_ui();
        } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            rid_radio_set_paused(!rid_radio_paused());
            if (!bsp_lvgl_lock(100)) {
                return;
            }
            rid_ui_set_hint(rid_radio_paused() ? "已暂停  OK长按:恢复" :
                            "OK:详情  长按:暂停  UP/DOWN:选择");
            bsp_lvgl_unlock();
        }
        return;
    }

    /* DETAIL 页 */
    if ((ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) ||
        (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) ||
        (ev == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN))) {
        if (!bsp_lvgl_lock(100)) {
            return;
        }
        rid_ui_set_page(RID_UI_PAGE_LIST);
        bsp_lvgl_unlock();
        refresh_ui();
    }
}

static void input_task(void *arg)
{
    (void)arg;
    input_event_t ev;
    for (;;) {
        if (xQueueReceive(s_input_queue, &ev, portMAX_DELAY) == pdTRUE) {
            process_key(ev.btn, ev.event);
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_queue) {
        return;
    }
    const input_event_t item = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &item, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "FoloRID 开源 Remote ID 接收器启动");
    ESP_LOGI(TAG, "平台 ESP32-C3  Flash 8MB  屏 %dx%d", BSP_LCD_W, BSP_LCD_H);

    (void)bsp_i2c_init();
    (void)bsp_battery_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败 MOSI=%d SCLK=%d CS=%d DC=%d BL=%d",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    rid_store_init(&s_store);
    rid_ui_fonts_init();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue || xTaskCreate(input_task, "rid_in", 4096, NULL, 5, &s_input_task) != pdPASS) {
        ESP_LOGE(TAG, "按键任务创建失败");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
        return;
    }

    if (bsp_lvgl_lock(1000)) {
        rid_ui_create();
        rid_ui_set_status(RID_UI_SCAN_STARTING, 0, 0, 0, bsp_battery_soc());
        rid_ui_set_hint("正在启动射频扫描...");
        bsp_lvgl_unlock();
    }

    s_radio_ok = (rid_radio_start(&s_store) == ESP_OK);
    if (!s_radio_ok) {
        ESP_LOGE(TAG, "RID 射频启动失败");
    } else {
        ESP_LOGI(TAG, "RID 射频已启动");
    }

    s_ui_timer = lv_timer_create(ui_timer_cb, UI_REFRESH_MS, NULL);
    s_ready = true;
    ESP_LOGI(TAG, "FoloRID 就绪 radio=%d", s_radio_ok);
}
