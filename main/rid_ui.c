// main/rid_ui.c —— RID 接收器界面实现：深色值守终端 + 中文字体
#include "rid_ui.h"
#include "bsp_display.h"

#include <stdio.h>
#include <string.h>

/*
 * 视觉设计锚点：航空 ATC / 无线电频谱值守终端。
 * 背景近黑、面板深蓝灰、信号青强调、告警琥珀/红。
 */
#define RID_COL_BG       0x070B14
#define RID_COL_PANEL    0x0F172A
#define RID_COL_PANEL2   0x111C33
#define RID_COL_LINE     0x1E293B
#define RID_COL_TEXT     0xE2E8F0
#define RID_COL_MUTED    0x64748B
#define RID_COL_ACCENT   0x22D3EE
#define RID_COL_GREEN    0x34D399
#define RID_COL_AMBER    0xF59E0B
#define RID_COL_RED      0xF87171
#define RID_COL_SELECT   0x134E4A

/* 应用中文字体（由 assets/fonts 生成） */
LV_FONT_DECLARE(rid_font_16);

/* 可写 fallback 描述符：缺字时回退到 Montserrat 14 */
static lv_font_t s_font_ui;
static bool s_font_ready;

/* 界面对象 */
static lv_obj_t *s_scr;
static lv_obj_t *s_top_bar;
static lv_obj_t *s_title;
static lv_obj_t *s_battery;
static lv_obj_t *s_scan_dot;
static lv_obj_t *s_status_line;
static lv_obj_t *s_list_panel;
static lv_obj_t *s_rows[RID_STORE_MAX_UAVS];
static lv_obj_t *s_row_labels[RID_STORE_MAX_UAVS];
static lv_obj_t *s_detail_panel;
static lv_obj_t *s_detail_body;
static lv_obj_t *s_hint;
static rid_ui_page_t s_page = RID_UI_PAGE_LIST;

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          uint32_t color)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, font, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    return lab;
}

static lv_obj_t *mk_panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_radius(p, 6, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(RID_COL_LINE), 0);
    lv_obj_set_style_pad_all(p, 6, 0);
    return p;
}

void rid_ui_fonts_init(void)
{
    if (s_font_ready) {
        return;
    }
    /* 拷贝静态字体描述符；应用子集已覆盖全部界面中文，避免 fallback 产生方框 */
    s_font_ui = rid_font_16;
    s_font_ui.fallback = &lv_font_montserrat_14;
    s_font_ready = true;
}

bool rid_ui_font_has_glyph(uint32_t codepoint)
{
    if (!s_font_ready) {
        return false;
    }
    lv_font_glyph_dsc_t g = { 0 };
    return lv_font_get_glyph_dsc(&s_font_ui, &g, codepoint, 0) && !g.is_placeholder;
}

static void build_list_page(void)
{
    s_list_panel = lv_obj_create(s_scr);
    lv_obj_remove_flag(s_list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_list_panel, 8, 56);
    lv_obj_set_size(s_list_panel, 224, 220);
    lv_obj_set_style_bg_color(s_list_panel, lv_color_hex(RID_COL_PANEL), 0);
    lv_obj_set_style_border_width(s_list_panel, 0, 0);
    lv_obj_set_style_radius(s_list_panel, 8, 0);
    lv_obj_set_style_pad_all(s_list_panel, 4, 0);

    for (int i = 0; i < RID_STORE_MAX_UAVS; i++) {
        int y = 2 + i * 26;
        s_rows[i] = lv_obj_create(s_list_panel);
        lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_rows[i], 2, y);
        lv_obj_set_size(s_rows[i], 212, 24);
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(RID_COL_PANEL2), 0);
        lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_rows[i], 0, 0);
        lv_obj_set_style_radius(s_rows[i], 4, 0);
        lv_obj_set_style_pad_hor(s_rows[i], 4, 0);

        s_row_labels[i] = mk_label(s_rows[i], "", &s_font_ui, RID_COL_MUTED);
        lv_label_set_long_mode(s_row_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_row_labels[i], 200);
        lv_obj_align(s_row_labels[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_detail_page(void)
{
    s_detail_panel = mk_panel(s_scr, 8, 56, 224, 220, RID_COL_PANEL);
    /* 详情可滚动，避免长文本被面板裁剪 */
    lv_obj_set_scroll_dir(s_detail_panel, LV_DIR_VER);
    lv_obj_set_style_pad_all(s_detail_panel, 6, 0);
    s_detail_body = mk_label(s_detail_panel, "", &s_font_ui, RID_COL_TEXT);
    lv_obj_set_width(s_detail_body, 206);
    lv_obj_align(s_detail_body, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_long_mode(s_detail_body, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_detail_panel, LV_OBJ_FLAG_HIDDEN);
}

void rid_ui_create(void)
{
    rid_ui_fonts_init();

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(RID_COL_BG), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    /* 顶栏 */
    s_top_bar = lv_obj_create(s_scr);
    lv_obj_remove_flag(s_top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_top_bar, 0, 0);
    lv_obj_set_size(s_top_bar, 240, 44);
    lv_obj_set_style_bg_color(s_top_bar, lv_color_hex(RID_COL_PANEL), 0);
    lv_obj_set_style_border_width(s_top_bar, 0, 0);
    lv_obj_set_style_radius(s_top_bar, 0, 0);
    lv_obj_set_style_pad_all(s_top_bar, 0, 0);

    /* 扫描指示点 */
    s_scan_dot = lv_obj_create(s_top_bar);
    lv_obj_remove_flag(s_scan_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_scan_dot, 10, 16);
    lv_obj_set_size(s_scan_dot, 10, 10);
    lv_obj_set_style_radius(s_scan_dot, 5, 0);
    lv_obj_set_style_bg_color(s_scan_dot, lv_color_hex(RID_COL_MUTED), 0);
    lv_obj_set_style_border_width(s_scan_dot, 0, 0);

    s_title = mk_label(s_top_bar, "RID接收器", &s_font_ui, RID_COL_ACCENT);
    lv_obj_set_pos(s_title, 26, 8);
    lv_obj_set_width(s_title, 100);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);

    /* 右上角电量：缩短文案，避免与标题挤压裁剪 */
    s_battery = mk_label(s_top_bar, "--%", &s_font_ui, RID_COL_MUTED);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_battery, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_battery, 56);

    /* 状态行：缩短，保证 240px 内可完整显示 */
    s_status_line = mk_label(s_scr, "启动中...", &s_font_ui, RID_COL_MUTED);
    lv_obj_set_pos(s_status_line, 10, 46);
    lv_obj_set_width(s_status_line, 220);
    lv_label_set_long_mode(s_status_line, LV_LABEL_LONG_DOT);

    build_list_page();
    build_detail_page();

    s_hint = mk_label(s_scr, "UP/DOWN选择 OK详情 长按暂停", &s_font_ui, RID_COL_MUTED);
    lv_obj_set_pos(s_hint, 4, 288);
    lv_obj_set_width(s_hint, 232);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_DOT);

    s_page = RID_UI_PAGE_LIST;
    lv_screen_load(s_scr);
}

void rid_ui_destroy(void)
{
    if (s_scr) {
        lv_obj_delete(s_scr);
    }
    s_scr = NULL;
    s_top_bar = s_title = s_battery = s_scan_dot = NULL;
    s_status_line = s_list_panel = s_detail_panel = s_detail_body = s_hint = NULL;
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_row_labels, 0, sizeof(s_row_labels));
}

void rid_ui_set_page(rid_ui_page_t page)
{
    s_page = page;
    if (!s_scr) {
        return;
    }
    if (page == RID_UI_PAGE_LIST) {
        lv_obj_remove_flag(s_list_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail_panel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "UP/DOWN选择 OK详情 长按暂停");
    } else {
        lv_obj_add_flag(s_list_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_detail_panel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "OK/长按返回列表");
    }
}

rid_ui_page_t rid_ui_page(void)
{
    return s_page;
}

void rid_ui_set_hint(const char *text)
{
    if (s_hint && text) {
        lv_label_set_text(s_hint, text);
    }
}

void rid_ui_set_status(rid_ui_scan_state_t scan_state, int uav_count,
                       uint16_t wifi_hits, uint16_t ble_hits, int battery_soc)
{
    if (!s_status_line) {
        return;
    }
    const char *state_text = "停止";
    uint32_t dot = RID_COL_MUTED;
    switch (scan_state) {
    case RID_UI_SCAN_STARTING:
        state_text = "启动中";
        dot = RID_COL_AMBER;
        break;
    case RID_UI_SCAN_RUNNING:
        state_text = "扫描中";
        dot = RID_COL_GREEN;
        break;
    case RID_UI_SCAN_PAUSED:
        state_text = "已暂停";
        dot = RID_COL_AMBER;
        break;
    case RID_UI_SCAN_FAILED:
        state_text = "扫描失败";
        dot = RID_COL_RED;
        break;
    default:
        break;
    }
    if (s_scan_dot) {
        lv_obj_set_style_bg_color(s_scan_dot, lv_color_hex(dot), 0);
    }
    lv_label_set_text_fmt(s_status_line, "%s 目标%d W%u B%u",
                          state_text, uav_count, wifi_hits, ble_hits);

    if (s_battery) {
        if (battery_soc < 0) {
            lv_label_set_text(s_battery, "--%");
        } else if (battery_soc > 100) {
            battery_soc = 100;
        }
        if (battery_soc >= 0) {
            lv_label_set_text_fmt(s_battery, "%d%%", battery_soc);
            uint32_t c = battery_soc <= 15 ? RID_COL_RED :
                         battery_soc <= 30 ? RID_COL_AMBER : RID_COL_MUTED;
            lv_obj_set_style_text_color(s_battery, lv_color_hex(c), 0);
        }
    }
}

static uint32_t status_color(rid_status_t st)
{
    switch (st) {
    case RID_STATUS_AIRBORNE:
        return RID_COL_GREEN;
    case RID_STATUS_EMERGENCY:
        return RID_COL_RED;
    case RID_STATUS_FAIL:
        return RID_COL_RED;
    case RID_STATUS_GROUND:
        return RID_COL_AMBER;
    default:
        return RID_COL_MUTED;
    }
}

void rid_ui_refresh(const rid_store_t *store, int selected_index,
                    rid_ui_scan_state_t scan_state, uint32_t now_ms)
{
    if (!s_scr || !store) {
        return;
    }
    int idx[RID_STORE_MAX_UAVS];
    int n = rid_store_list_indices(store, idx, RID_STORE_MAX_UAVS);

    if (s_page == RID_UI_PAGE_LIST) {
        for (int row = 0; row < RID_STORE_MAX_UAVS; row++) {
            if (row >= n || !s_rows[row]) {
                if (s_rows[row]) {
                    lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
                }
                continue;
            }
            const rid_uav_t *u = &store->slots[idx[row]];
            lv_obj_remove_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
            bool sel = (idx[row] == selected_index);
            lv_obj_set_style_bg_opa(s_rows[row], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(s_rows[row],
                                      lv_color_hex(sel ? RID_COL_SELECT : RID_COL_PANEL2), 0);

            char idpart[24];
            if (u->has_basic && u->uas_id[0]) {
                snprintf(idpart, sizeof(idpart), "%.16s", u->uas_id);
            } else {
                snprintf(idpart, sizeof(idpart), "%02X%02X%02X",
                         u->mac[3], u->mac[4], u->mac[5]);
            }
            char line[80];
            if (u->has_location) {
                snprintf(line, sizeof(line), "%s  %.0fm  %.0fm/s  %s",
                         idpart, (double)u->alt_m, (double)u->speed_mps,
                         rid_odid_status_text(u->status));
            } else {
                snprintf(line, sizeof(line), "%s  %s  %ddBm",
                         idpart, rid_odid_link_text(u->link), (int)u->rssi);
            }
            lv_label_set_text(s_row_labels[row], line);
            lv_obj_set_style_text_color(s_row_labels[row],
                                        lv_color_hex(sel ? RID_COL_TEXT : status_color(u->status)),
                                        0);
        }
        if (n == 0) {
            for (int row = 0; row < RID_STORE_MAX_UAVS; row++) {
                if (s_rows[row]) {
                    lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
                }
            }
            if (s_status_line && scan_state == RID_UI_SCAN_RUNNING) {
                lv_label_set_text(s_hint, "未发现无人机广播 OK关于");
            }
        }
    } else if (s_page == RID_UI_PAGE_DETAIL && s_detail_body) {
        if (selected_index < 0 || selected_index >= RID_STORE_MAX_UAVS ||
            !store->slots[selected_index].used) {
            lv_label_set_text(s_detail_body, "无目标数据");
            return;
        }
        const rid_uav_t *u = &store->slots[selected_index];
        char buf[420];
        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "飞行器ID %s\n操作员ID %s\n",
                        u->has_basic && u->uas_id[0] ? u->uas_id : "-",
                        u->has_operator && u->op_id[0] ? u->op_id : "-");
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "状态%s 链路%s %ddBm\n",
                        rid_odid_status_text(u->status),
                        rid_odid_link_text(u->link), (int)u->rssi);
        if (u->has_location) {
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                            "纬度 %.5f\n经度 %.5f\n海拔 %.0fm %s%.0fm\n"
                            "速度 %.1fm/s 航向 %.0f°\n",
                            u->lat, u->lon, (double)u->alt_m,
                            u->height_m >= 0 ? "离地" : "高",
                            (double)u->height_m,
                            (double)u->speed_mps, (double)u->heading_deg);
        } else {
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "位置 未收到\n");
        }
        if (u->has_system) {
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                            "操作员位置 %.4f, %.4f\n",
                            (double)u->op_lat, (double)u->op_lon);
        }
        uint32_t age_s = (now_ms >= u->last_ms) ? (now_ms - u->last_ms) / 1000u : 0;
        snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                 "更新 %lus前 帧%lu\n%02X:%02X:%02X:%02X:%02X:%02X",
                 (unsigned long)age_s, (unsigned long)u->hit_count,
                 u->mac[0], u->mac[1], u->mac[2], u->mac[3], u->mac[4], u->mac[5]);
        lv_label_set_text(s_detail_body, buf);
        lv_obj_scroll_to_y(s_detail_panel, 0, LV_ANIM_OFF);
    }
}
