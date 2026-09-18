// main/rid_odid.h —— OpenDroneID / ASTM F3411 精简解码器（纯逻辑，可 host 测试）
//
// 覆盖接收端所需消息：BasicID / Location / OperatorID / System，
// 以及 WiFi Beacon 厂商 IE 与 BLE 0xFFFA 服务广播中的单条/打包消息。
// 不包含编码、鉴权验签与 Wi-Fi NAN 完整栈。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rid_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ODID 单条消息固定长度（ASTM F3411）。 */
#define RID_ODID_MSG_SIZE 25

/** WiFi 厂商 IE / BLE Service Data 中的 OpenDroneID OUI。 */
#define RID_ODID_OUI_0 0xFA
#define RID_ODID_OUI_1 0x0B
#define RID_ODID_OUI_2 0xBC

/** NAN 行动帧目的地址前缀（51:6f:9a:01:00:00）。 */
#define RID_ODID_NAN_DEST_0 0x51
#define RID_ODID_NAN_DEST_1 0x6F
#define RID_ODID_NAN_DEST_2 0x9A
#define RID_ODID_NAN_DEST_3 0x01
#define RID_ODID_NAN_DEST_4 0x00
#define RID_ODID_NAN_DEST_5 0x00

/** BLE OpenDroneID 服务 UUID 16-bit：0xFFFA */
#define RID_ODID_BLE_SERVICE_LSB 0xFA
#define RID_ODID_BLE_SERVICE_MSB 0xFF

/** 解码结果摘要，供 store/UI 消费。 */
typedef struct {
    bool has_basic;
    bool has_location;
    bool has_operator;
    bool has_system;
    char uas_id[RID_ID_SIZE];
    char op_id[RID_ID_SIZE];
    rid_status_t status;
    double lat;
    double lon;
    float alt_m;
    float height_m;
    float speed_mps;
    float heading_deg;
    float op_lat;
    float op_lon;
} rid_odid_result_t;

/**
 * 解析单条 25 字节 ODID 消息，把结果合入 result（OR 语义，不覆盖已有字段）。
 * @param msg 至少 RID_ODID_MSG_SIZE 字节
 * @return true 表示至少解析出一类有效消息
 */
bool rid_odid_parse_message(const uint8_t *msg, rid_odid_result_t *result);

/**
 * 解析消息包（MessageType=0xF）。
 * @param pack 指向打包消息首字节
 * @param len  缓冲区长度
 * @return true 表示至少解析出一条子消息
 */
bool rid_odid_parse_pack(const uint8_t *pack, size_t len, rid_odid_result_t *result);

/**
 * 从 WiFi Beacon / Probe 帧负载中查找 ODID 厂商 IE 并解析。
 * @param frame 802.11 帧起始（含 MAC 头）
 * @param len   帧长度（rx_ctrl.sig_len）
 * @return true 表示找到并解析了 ODID 内容
 */
bool rid_odid_parse_wifi_beacon(const uint8_t *frame, size_t len, rid_odid_result_t *result);

/**
 * 判断是否为 NAN 行动帧中的 ODID 消息包。
 */
bool rid_odid_parse_wifi_nan(const uint8_t *frame, size_t len, rid_odid_result_t *result);

/**
 * 从 BLE 广播负载中解析 ODID（服务 UUID 0xFFFA 的 Service Data）。
 * @param adv   广播 payload
 * @param len   payload 长度
 * @return true 表示解析成功
 */
bool rid_odid_parse_ble_adv(const uint8_t *adv, size_t len, rid_odid_result_t *result);

/** 把 result 合并进 store 中对应 MAC 的目标，并更新命中统计。 */
void rid_odid_apply_to_store(rid_store_t *store, const uint8_t mac[6],
                             const rid_odid_result_t *result, rid_link_t link,
                             int8_t rssi, uint32_t now_ms);

/** 飞行状态枚举的中文短名（静态字符串，勿释放）。 */
const char *rid_odid_status_text(rid_status_t status);

/** 目标 ID 类型/机型的中文短名。UAS ID 首字节或展示用。 */
const char *rid_odid_link_text(rid_link_t link);

#ifdef __cplusplus
}
#endif
