// tests/test_rid_store.c —— RID 目标库主机逻辑测试
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rid_store.h"
#include "rid_odid.h"

static void test_touch_and_merge(void)
{
    rid_store_t store;
    rid_store_init(&store);
    uint8_t mac[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };

    rid_uav_t *u = rid_store_touch(&store, mac, 1000);
    assert(u != NULL);
    assert(u->used);
    assert(memcmp(u->mac, mac, 6) == 0);

    rid_store_merge_basic(u, "SN-ABC123");
    rid_store_merge_location(u, RID_STATUS_AIRBORNE, 31.2304, 121.4737,
                             120.5f, 80.0f, 8.5f, 90.0f);
    rid_store_merge_operator(u, "OP-42");
    rid_store_merge_system(u, 31.20f, 121.40f);
    rid_store_mark_hit(&store, u, RID_LINK_WIFI_BEACON, -55, 1200);

    assert(u->has_basic && strcmp(u->uas_id, "SN-ABC123") == 0);
    assert(u->has_location && u->status == RID_STATUS_AIRBORNE);
    assert(u->lat > 31.2 && u->lon > 121.4);
    assert(u->has_operator && strcmp(u->op_id, "OP-42") == 0);
    assert(u->has_system);
    assert(store.wifi_hits == 1);
    assert(rid_store_active_count(&store) == 1);

    // 同 MAC 再次 touch 应复用槽位
    rid_uav_t *u2 = rid_store_touch(&store, mac, 2000);
    assert(u2 == u);
    assert(u->last_ms == 2000);
}

static void test_expire(void)
{
    rid_store_t store;
    rid_store_init(&store);
    uint8_t mac1[6] = { 1, 2, 3, 4, 5, 6 };
    uint8_t mac2[6] = { 6, 5, 4, 3, 2, 1 };
    rid_store_touch(&store, mac1, 1000);
    rid_store_touch(&store, mac2, 50000);
    assert(rid_store_active_count(&store) == 2);

    int removed = rid_store_expire(&store, 1000 + RID_STORE_EXPIRE_MS + 1,
                                   RID_STORE_EXPIRE_MS);
    assert(removed == 1);
    assert(rid_store_active_count(&store) == 1);
}

static void test_list_order(void)
{
    rid_store_t store;
    rid_store_init(&store);
    uint8_t a[6] = { 0xAA, 0, 0, 0, 0, 1 };
    uint8_t b[6] = { 0xBB, 0, 0, 0, 0, 2 };
    uint8_t c[6] = { 0xCC, 0, 0, 0, 0, 3 };
    rid_uav_t *ua = rid_store_touch(&store, a, 100);
    rid_uav_t *ub = rid_store_touch(&store, b, 300);
    rid_uav_t *uc = rid_store_touch(&store, c, 200);
    assert(ua && ub && uc);
    int idx[8];
    int n = rid_store_list_indices(&store, idx, 8);
    assert(n == 3);
    assert(store.slots[idx[0]].last_ms == 300);
    assert(store.slots[idx[1]].last_ms == 200);
    assert(store.slots[idx[2]].last_ms == 100);
}

static void test_invalid_location(void)
{
    rid_store_t store;
    rid_store_init(&store);
    uint8_t mac[6] = { 9, 9, 9, 9, 9, 9 };
    rid_uav_t *u = rid_store_touch(&store, mac, 1);
    rid_store_merge_location(u, RID_STATUS_UNKNOWN, 999.0, 0.0, 0, 0, 0, 0);
    assert(!u->has_location);
}

static void test_odid_basic_message(void)
{
    uint8_t msg[25];
    memset(msg, 0, sizeof(msg));
    msg[0] = (0x0 << 4); // BasicID
    msg[1] = 0x12;       // IDType=1 UAType=2
    memcpy(&msg[2], "TEST-UAV-001", 12);
    rid_odid_result_t res;
    memset(&res, 0, sizeof(res));
    assert(rid_odid_parse_message(msg, &res));
    assert(res.has_basic);
    assert(strcmp(res.uas_id, "TEST-UAV-001") == 0);
}

static void test_odid_operator(void)
{
    uint8_t msg[25];
    memset(msg, 0, sizeof(msg));
    msg[0] = (0x5 << 4);
    memcpy(&msg[2], "CN-OP-999", 9);
    rid_odid_result_t res;
    memset(&res, 0, sizeof(res));
    assert(rid_odid_parse_message(msg, &res));
    assert(res.has_operator);
    assert(strcmp(res.op_id, "CN-OP-999") == 0);
}

static void test_odid_ble_adv(void)
{
    // AD: len=0x1B type=0x16 uuid=FF FA + ODID Basic message
    uint8_t adv[32];
    memset(adv, 0, sizeof(adv));
    uint8_t *msg = &adv[4];
    msg[0] = 0x00; // Basic
    memcpy(&msg[2], "BLE-UAV", 7);
    adv[0] = 1 + 2 + 25; // type + uuid + msg
    adv[1] = 0x16;
    adv[2] = 0xFA;
    adv[3] = 0xFF;
    rid_odid_result_t res;
    memset(&res, 0, sizeof(res));
    assert(rid_odid_parse_ble_adv(adv, 4 + 25, &res));
    assert(res.has_basic);
    assert(strcmp(res.uas_id, "BLE-UAV") == 0);
}

static void test_geo_distance(void)
{
    float d = rid_geo_distance_m(31.0, 121.0, 31.0, 121.0);
    assert(d >= 0.0f && d < 1.0f);
    float d2 = rid_geo_distance_m(31.0, 121.0, 32.0, 121.0);
    assert(d2 > 100000.0f && d2 < 120000.0f);
}

int main(void)
{
    test_touch_and_merge();
    test_expire();
    test_list_order();
    test_invalid_location();
    test_odid_basic_message();
    test_odid_operator();
    test_odid_ble_adv();
    test_geo_distance();
    printf("test_rid_store: PASS\n");
    return 0;
}
