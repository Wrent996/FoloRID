// main/rid_store.h —— 开源 RID 接收器：无人机目标库（纯逻辑，可 host 测试）
//
// 职责：
//   - 以 MAC/地址为键维护最多 RID_STORE_MAX_UAVS 个目标
//   - 合并 BasicID / Location / OperatorID / System 字段
//   - 按最近更新时间过期淘汰，并提供只读快照给 UI
//
// 线程模型：调用方负责加锁（应用层用 spinlock/mutex）；本模块本身不睡眠。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 目标库容量。屏幕一次可显示约 6 行，留出滚动缓冲。 */
#define RID_STORE_MAX_UAVS 8

/** 飞行器标识长度（ASTM F3411 UAS ID，含结尾 NUL）。 */
#define RID_ID_SIZE 20

/** 目标条目在多少毫秒无更新后视为过期。 */
#define RID_STORE_EXPIRE_MS 60000u

/** 单条消息来源链路。 */
typedef enum {
    RID_LINK_WIFI_BEACON = 0,
    RID_LINK_WIFI_NAN,
    RID_LINK_BLE,
} rid_link_t;

/** 飞行状态（对齐 ODID_status 简化子集）。 */
typedef enum {
    RID_STATUS_UNKNOWN = 0,
    RID_STATUS_GROUND,
    RID_STATUS_AIRBORNE,
    RID_STATUS_EMERGENCY,
    RID_STATUS_FAIL,
} rid_status_t;

/** 目标条目：UI 与协议解析共享的最终展示模型。 */
typedef struct {
    bool used;                       /**< 槽位是否占用 */
    uint8_t mac[6];                  /**< 发射端 MAC（WiFi）或 BLE 地址 */
    bool has_basic;                  /**< 是否收到过 BasicID */
    bool has_location;               /**< 是否收到过 Location */
    bool has_operator;               /**< 是否收到过 OperatorID */
    bool has_system;                 /**< 是否收到过 System/操作员位置 */
    char uas_id[RID_ID_SIZE];       /**< 飞行器 ID（序列号/注册号等） */
    char op_id[RID_ID_SIZE];         /**< 操作员 ID */
    rid_link_t link;                 /**< 最近一次消息来源 */
    rid_status_t status;             /**< 飞行状态 */
    double lat;                      /**< 纬度（度）；无效为 0 且 has_location=false */
    double lon;                      /**< 经度（度） */
    float alt_m;                     /**< 海拔高度（米） */
    float height_m;                  /**< 离地/起飞高度（米） */
    float speed_mps;                 /**< 水平速度（米/秒） */
    float heading_deg;               /**< 航向（度，0-360） */
    float op_lat;                    /**< 操作员纬度 */
    float op_lon;                    /**< 操作员经度 */
    int8_t rssi;                     /**< 最近 RSSI */
    uint32_t last_ms;                /**< 最近更新时间（调用方提供单调毫秒） */
    uint32_t hit_count;              /**< 命中次数（用于排序参考） */
} rid_uav_t;

/** 目标库实例。字段对调用方透明，便于 host 测试直接检查。 */
typedef struct {
    rid_uav_t slots[RID_STORE_MAX_UAVS];
    uint32_t total_hits;             /**< 历史累计命中（含已过期） */
    uint16_t wifi_hits;              /**< WiFi 链路累计 */
    uint16_t ble_hits;               /**< BLE 链路累计 */
} rid_store_t;

/** 清空目标库。 */
void rid_store_init(rid_store_t *store);

/**
 * 按 MAC 查找或创建目标槽位。
 * @param mac 6 字节地址，不得为 NULL
 * @return 槽位指针；库满时复用“最久未更新”的槽位，永不返回 NULL（store 合法时）
 */
rid_uav_t *rid_store_touch(rid_store_t *store, const uint8_t mac[6], uint32_t now_ms);

/**
 * 将一条已解码的 ODID 字段合并进目标。
 * 字段指针为 NULL 时表示该消息类型本次未携带，不覆盖已有值。
 */
void rid_store_merge_basic(rid_uav_t *uav, const char *uas_id);
void rid_store_merge_location(rid_uav_t *uav, rid_status_t status, double lat, double lon,
                             float alt_m, float height_m, float speed_mps, float heading_deg);
void rid_store_merge_operator(rid_uav_t *uav, const char *op_id);
void rid_store_merge_system(rid_uav_t *uav, float op_lat, float op_lon);
void rid_store_mark_hit(rid_store_t *store, rid_uav_t *uav, rid_link_t link,
                        int8_t rssi, uint32_t now_ms);

/**
 * 淘汰 now_ms - last_ms > expire_ms 的目标。
 * @return 本次淘汰数量
 */
int rid_store_expire(rid_store_t *store, uint32_t now_ms, uint32_t expire_ms);

/** 返回当前占用槽位数量。 */
int rid_store_active_count(const rid_store_t *store);

/**
 * 按“最近更新时间降序”收集活动目标索引。
 * @param out_idx 输出索引数组，容量至少 max_out
 * @return 实际写入数量
 */
int rid_store_list_indices(const rid_store_t *store, int *out_idx, int max_out);

/** 计算两个经纬度之间的近似球面距离（米）；无效输入返回 <0。 */
float rid_geo_distance_m(double lat1, double lon1, double lat2, double lon2);

#ifdef __cplusplus
}
#endif
