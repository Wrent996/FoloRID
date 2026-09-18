// main/rid_radio.h —— RID 射频扫描：WiFi 混杂 + BLE 观察者
#pragma once

#include "esp_err.h"
#include "rid_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 射频初始化/启动/停止。启动后后台任务解析 ODID 并写入 store。 */
esp_err_t rid_radio_start(rid_store_t *store);
esp_err_t rid_radio_stop(void);

/** 暂停/恢复解析（射频可保持运行）。 */
void rid_radio_set_paused(bool paused);
bool rid_radio_paused(void);

/** 当前扫描状态：0=停止，1=运行，2=失败 */
int rid_radio_state(void);

/** WiFi 信道（1-13），用于状态展示；未启动返回 0。 */
int rid_radio_wifi_channel(void);

#ifdef __cplusplus
}
#endif
