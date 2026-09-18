// main/rid_ui.h —— 开源 RID 接收器界面（深色值守终端风格，不沿用 demo 主题）
//
// 与 ui_pixel 天空/草地主题分离：本应用自建深色 ATC/频谱值守视觉。
// 电量仍按约定显示在右上角（本主题无白云装饰，置于右上空白区）。
#pragma once

#include "lvgl.h"
#include "rid_store.h"
#include "rid_odid.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 页面模式。 */
typedef enum {
    RID_UI_PAGE_LIST = 0,   /**< 目标列表 / 扫描总览 */
    RID_UI_PAGE_DETAIL,     /**< 单机详情 */
} rid_ui_page_t;

/** 扫描运行状态。 */
typedef enum {
    RID_UI_SCAN_OFF = 0,
    RID_UI_SCAN_STARTING,
    RID_UI_SCAN_RUNNING,
    RID_UI_SCAN_PAUSED,
    RID_UI_SCAN_FAILED,
} rid_ui_scan_state_t;

/** 初始化中文应用字体（含 Montserrat fallback）。须在 LVGL 初始化后调用一次。 */
void rid_ui_fonts_init(void);

/** 检查应用字体是否覆盖码点（供验收与测试）。 */
bool rid_ui_font_has_glyph(uint32_t codepoint);

/** 创建并载入主界面。调用方须持有 bsp_lvgl_lock()。 */
void rid_ui_create(void);

/** 删除界面对象。调用方须持有 bsp_lvgl_lock()，且先停止扫描。 */
void rid_ui_destroy(void);

/** 将 store 快照刷新到界面。调用方须持有 bsp_lvgl_lock()。 */
void rid_ui_refresh(const rid_store_t *store, int selected_index,
                    rid_ui_scan_state_t scan_state, uint32_t now_ms);

/** 切换页面。 */
void rid_ui_set_page(rid_ui_page_t page);

/** 读取当前页面。 */
rid_ui_page_t rid_ui_page(void);

/** 设置底部提示文案。 */
void rid_ui_set_hint(const char *text);

/** 更新状态栏（扫描状态、计数、电量）。battery_soc=-1 表示不可用。 */
void rid_ui_set_status(rid_ui_scan_state_t scan_state, int uav_count,
                       uint16_t wifi_hits, uint16_t ble_hits, int battery_soc);

#ifdef __cplusplus
}
#endif
