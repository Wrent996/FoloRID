// main/rid_store.c —— RID 目标库实现（无 ESP-IDF / LVGL 依赖）
#include "rid_store.h"

#include <math.h>
#include <string.h>

/** 地球平均半径（米），用于近似大圆距离。 */
#define RID_EARTH_RADIUS_M 6371000.0

void rid_store_init(rid_store_t *store)
{
    if (!store) {
        return;
    }
    memset(store, 0, sizeof(*store));
}

rid_uav_t *rid_store_touch(rid_store_t *store, const uint8_t mac[6], uint32_t now_ms)
{
    if (!store || !mac) {
        return NULL;
    }

    // 先按 MAC 精确匹配
    for (int i = 0; i < RID_STORE_MAX_UAVS; i++) {
        rid_uav_t *slot = &store->slots[i];
        if (slot->used && memcmp(slot->mac, mac, 6) == 0) {
            slot->last_ms = now_ms;
            return slot;
        }
    }

    // 找空槽
    for (int i = 0; i < RID_STORE_MAX_UAVS; i++) {
        rid_uav_t *slot = &store->slots[i];
        if (!slot->used) {
            memset(slot, 0, sizeof(*slot));
            slot->used = true;
            memcpy(slot->mac, mac, 6);
            slot->last_ms = now_ms;
            return slot;
        }
    }

    // 库满：淘汰最久未更新的槽位并复用
    int oldest = 0;
    for (int i = 1; i < RID_STORE_MAX_UAVS; i++) {
        if (store->slots[i].last_ms < store->slots[oldest].last_ms) {
            oldest = i;
        }
    }
    rid_uav_t *slot = &store->slots[oldest];
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->mac, mac, 6);
    slot->last_ms = now_ms;
    return slot;
}

void rid_store_merge_basic(rid_uav_t *uav, const char *uas_id)
{
    if (!uav || !uas_id || !uas_id[0]) {
        return;
    }
    memset(uav->uas_id, 0, sizeof(uav->uas_id));
    strncpy(uav->uas_id, uas_id, sizeof(uav->uas_id) - 1);
    uav->has_basic = true;
}

void rid_store_merge_location(rid_uav_t *uav, rid_status_t status, double lat, double lon,
                             float alt_m, float height_m, float speed_mps, float heading_deg)
{
    if (!uav) {
        return;
    }
    // 仅接受合法经纬度，避免把无效包写进 UI
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return;
    }
    uav->status = status;
    uav->lat = lat;
    uav->lon = lon;
    uav->alt_m = alt_m;
    uav->height_m = height_m;
    uav->speed_mps = speed_mps;
    uav->heading_deg = heading_deg;
    uav->has_location = true;
}

void rid_store_merge_operator(rid_uav_t *uav, const char *op_id)
{
    if (!uav || !op_id || !op_id[0]) {
        return;
    }
    memset(uav->op_id, 0, sizeof(uav->op_id));
    strncpy(uav->op_id, op_id, sizeof(uav->op_id) - 1);
    uav->has_operator = true;
}

void rid_store_merge_system(rid_uav_t *uav, float op_lat, float op_lon)
{
    if (!uav) {
        return;
    }
    if (op_lat < -90.0f || op_lat > 90.0f || op_lon < -180.0f || op_lon > 180.0f) {
        return;
    }
    uav->op_lat = op_lat;
    uav->op_lon = op_lon;
    uav->has_system = true;
}

void rid_store_mark_hit(rid_store_t *store, rid_uav_t *uav, rid_link_t link,
                        int8_t rssi, uint32_t now_ms)
{
    if (!store || !uav) {
        return;
    }
    uav->last_ms = now_ms;
    uav->rssi = rssi;
    uav->link = link;
    uav->hit_count++;
    store->total_hits++;
    if (link == RID_LINK_BLE) {
        store->ble_hits++;
    } else {
        store->wifi_hits++;
    }
}

int rid_store_expire(rid_store_t *store, uint32_t now_ms, uint32_t expire_ms)
{
    if (!store) {
        return 0;
    }
    int removed = 0;
    for (int i = 0; i < RID_STORE_MAX_UAVS; i++) {
        rid_uav_t *slot = &store->slots[i];
        if (!slot->used) {
            continue;
        }
        // 无符号减法处理回绕：now < last 时视为未过期
        if ((now_ms - slot->last_ms) > expire_ms) {
            memset(slot, 0, sizeof(*slot));
            removed++;
        }
    }
    return removed;
}

int rid_store_active_count(const rid_store_t *store)
{
    if (!store) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < RID_STORE_MAX_UAVS; i++) {
        if (store->slots[i].used) {
            n++;
        }
    }
    return n;
}

int rid_store_list_indices(const rid_store_t *store, int *out_idx, int max_out)
{
    if (!store || !out_idx || max_out <= 0) {
        return 0;
    }
    // 收集活动索引
    int active[RID_STORE_MAX_UAVS];
    int n = 0;
    for (int i = 0; i < RID_STORE_MAX_UAVS; i++) {
        if (store->slots[i].used && n < RID_STORE_MAX_UAVS) {
            active[n++] = i;
        }
    }
    // 按 last_ms 降序插入排序（n 很小）
    for (int i = 1; i < n; i++) {
        int key = active[i];
        int j = i - 1;
        while (j >= 0 && store->slots[active[j]].last_ms < store->slots[key].last_ms) {
            active[j + 1] = active[j];
            j--;
        }
        active[j + 1] = key;
    }
    int out_n = n < max_out ? n : max_out;
    memcpy(out_idx, active, (size_t)out_n * sizeof(int));
    return out_n;
}

float rid_geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    if (lat1 < -90.0 || lat1 > 90.0 || lat2 < -90.0 || lat2 > 90.0 ||
        lon1 < -180.0 || lon1 > 180.0 || lon2 < -180.0 || lon2 > 180.0) {
        return -1.0f;
    }
    const double deg2rad = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * deg2rad;
    double dlon = (lon2 - lon1) * deg2rad;
    double a = sin(dlat * 0.5) * sin(dlat * 0.5) +
               cos(lat1 * deg2rad) * cos(lat2 * deg2rad) * sin(dlon * 0.5) * sin(dlon * 0.5);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(RID_EARTH_RADIUS_M * c);
}
