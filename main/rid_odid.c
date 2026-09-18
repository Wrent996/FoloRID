// main/rid_odid.c —— OpenDroneID 精简解码实现
//
// 数值换算对齐 ASTM F3411 / OpenDroneID core-c：
//   lat/lon: int32 * 1e-7
//   altitude: (raw - 1000) * 0.5
//   speed_h: 0.25 或 0.75 * raw（bit7=SpeedMult）
//   direction: 360/256 * raw
#include "rid_odid.h"

#include <math.h>
#include <string.h>

/** 消息类型（高 4 bit）。 */
#define ODID_MSG_BASIC  0x0
#define ODID_MSG_LOC    0x1
#define ODID_MSG_AUTH   0x2
#define ODID_MSG_SELF   0x3
#define ODID_MSG_SYSTEM 0x4
#define ODID_MSG_OP     0x5
#define ODID_MSG_PACK   0xF

static int in_range(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

static void copy_id(char *dst, size_t dst_sz, const uint8_t *src, size_t src_sz)
{
    size_t n = src_sz < (dst_sz - 1) ? src_sz : (dst_sz - 1);
    size_t i;
    for (i = 0; i < n; i++) {
        uint8_t c = src[i];
        // 停止在 NUL；过滤不可见控制字符，避免 UI 乱码
        if (c == 0) {
            break;
        }
        if (c < 0x20) {
            c = '.';
        }
        dst[i] = (char)c;
    }
    dst[i] = '\0';
}

static double decode_latlon(int32_t raw)
{
    return (double)raw * 1e-7;
}

static float decode_alt(uint16_t raw)
{
    return ((float)raw - 1000.0f) * 0.5f;
}

static float decode_speed_h(uint8_t raw)
{
    // bit7 为 SpeedMult：0 → 0.25 m/s 步进，1 → 0.75 m/s 步进
    uint8_t mag = raw & 0x7F;
    if (raw & 0x80) {
        return 0.75f * (float)mag;
    }
    return 0.25f * (float)mag;
}

static float decode_direction(uint8_t raw)
{
    return (float)raw * (360.0f / 256.0f);
}

static int32_t read_i32_le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static rid_status_t map_status(uint8_t raw)
{
    switch (raw & 0x0F) {
    case 1:
        return RID_STATUS_GROUND;
    case 2:
        return RID_STATUS_AIRBORNE;
    case 3:
        return RID_STATUS_EMERGENCY;
    case 4:
        return RID_STATUS_FAIL;
    default:
        return RID_STATUS_UNKNOWN;
    }
}

bool rid_odid_parse_message(const uint8_t *msg, rid_odid_result_t *result)
{
    if (!msg || !result) {
        return false;
    }
    uint8_t type = (uint8_t)(msg[0] >> 4);
    switch (type) {
    case ODID_MSG_BASIC: {
        // Byte1: IDType(7:4) | UAType(3:0); Byte2..21 UASID
        if (!in_range((msg[0] & 0x0F), 0, 15)) {
            return false;
        }
        copy_id(result->uas_id, sizeof(result->uas_id), &msg[2], 20);
        result->has_basic = true;
        return true;
    }
    case ODID_MSG_LOC: {
        // ASTM F3411 Location 编码布局（简化解析，取关键字段）
        // Byte0: type|proto; Byte1: Status|HeightType|EWDir|SpeedMult
        uint8_t b1 = msg[1];
        uint8_t status = (uint8_t)(b1 & 0x0F);
        uint8_t ew_dir = (uint8_t)((b1 >> 5) & 0x01);
        uint8_t speed_mult = (uint8_t)((b1 >> 4) & 0x01);
        // Byte2: Direction
        float dir = decode_direction(msg[2]);
        if (ew_dir) {
            // EWDirection=1 表示 90..270 区间已在 Direction 编码中体现；此处保留 raw 换算
        }
        (void)ew_dir;
        uint8_t speed_raw = msg[3];
        if (speed_mult) {
            speed_raw |= 0x80;
        }
        float speed = decode_speed_h(speed_raw);
        // Byte4: SpeedVertical
        int8_t vraw = (int8_t)msg[4];
        float vspeed = ((float)vraw - 62.0f) * 0.5f;
        (void)vspeed;
        // Byte5..8 Latitude, Byte9..12 Longitude (little endian int32)
        double lat = decode_latlon(read_i32_le(&msg[5]));
        double lon = decode_latlon(read_i32_le(&msg[9]));
        // Byte13..14 AltitudeBaro, Byte15..16 AltitudeGeo, Byte17..18 Height
        float alt_baro = decode_alt(read_u16_le(&msg[13]));
        float alt_geo = decode_alt(read_u16_le(&msg[15]));
        float height = decode_alt(read_u16_le(&msg[17]));
        float alt = (alt_geo > -900.0f) ? alt_geo : alt_baro;

        result->status = map_status(status);
        result->lat = lat;
        result->lon = lon;
        result->alt_m = alt;
        result->height_m = height;
        result->speed_mps = speed;
        result->heading_deg = dir;
        result->has_location = true;
        return true;
    }
    case ODID_MSG_OP: {
        // Byte0 type; Byte1 OperatorIdType; Byte2..21 OperatorId
        copy_id(result->op_id, sizeof(result->op_id), &msg[2], 20);
        result->has_operator = true;
        return true;
    }
    case ODID_MSG_SYSTEM: {
        // Byte1: OperatorLocationType|ClassificationType
        // Byte2..5 OperatorLatitude, Byte6..9 OperatorLongitude
        float olat = (float)decode_latlon(read_i32_le(&msg[2]));
        float olon = (float)decode_latlon(read_i32_le(&msg[6]));
        result->op_lat = olat;
        result->op_lon = olon;
        result->has_system = true;
        return true;
    }
    case ODID_MSG_SELF:
    case ODID_MSG_AUTH:
        // 接收端 UI 暂不展示，但视为有效帧
        return true;
    default:
        return false;
    }
}

bool rid_odid_parse_pack(const uint8_t *pack, size_t len, rid_odid_result_t *result)
{
    if (!pack || !result || len < 3) {
        return false;
    }
    if ((pack[0] >> 4) != ODID_MSG_PACK) {
        return false;
    }
    // Byte1: SingleMessageSize（应为 25）；Byte2: MsgPackSize
    uint8_t single = pack[1];
    uint8_t count = pack[2];
    if (single != RID_ODID_MSG_SIZE) {
        return false;
    }
    if (count > 9) {
        count = 9;
    }
    size_t need = 3u + (size_t)count * RID_ODID_MSG_SIZE;
    if (len < need) {
        count = (uint8_t)((len - 3) / RID_ODID_MSG_SIZE);
    }
    bool any = false;
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *msg = &pack[3 + (size_t)i * RID_ODID_MSG_SIZE];
        if (rid_odid_parse_message(msg, result)) {
            any = true;
        }
    }
    return any;
}

static bool parse_vendor_odid(const uint8_t *ie_val, size_t ie_len, rid_odid_result_t *result)
{
    // ie_val 指向 OUI 起始：OUI(3) + vendor type(1) + ODID payload
    if (ie_len < 4) {
        return false;
    }
    if (ie_val[0] != RID_ODID_OUI_0 || ie_val[1] != RID_ODID_OUI_1 ||
        ie_val[2] != RID_ODID_OUI_2) {
        return false;
    }
    const uint8_t *payload = &ie_val[4];
    size_t plen = ie_len - 4;
    if (plen < 1) {
        return false;
    }
    if ((payload[0] >> 4) == ODID_MSG_PACK) {
        return rid_odid_parse_pack(payload, plen, result);
    }
    if (plen >= RID_ODID_MSG_SIZE) {
        return rid_odid_parse_message(payload, result);
    }
    return false;
}

bool rid_odid_parse_wifi_beacon(const uint8_t *frame, size_t len, rid_odid_result_t *result)
{
    if (!frame || !result || len < 36) {
        return false;
    }
    // Beacon / Probe Response: Frame Control byte0 = 0x80 / 0x50
    uint8_t fc0 = frame[0];
    if (fc0 != 0x80 && fc0 != 0x50 && (fc0 & 0x0C) != 0x00) {
        // 仍尝试扫描 IEs，兼容部分实现
    }
    // 802.11 beacon 固定头 24 MAC + 12 beacon body = 36
    size_t offset = 36;
    bool any = false;
    while (offset + 2 <= len) {
        uint8_t id = frame[offset];
        uint8_t elen = frame[offset + 1];
        if (offset + 2 + elen > len) {
            break;
        }
        const uint8_t *val = &frame[offset + 2];
        if (id == 0xDD && elen >= 4) {
            // Parrot OUI 90:3a:e6 也使用 ODID 载荷，一并识别
            if ((val[0] == RID_ODID_OUI_0 && val[1] == RID_ODID_OUI_1 &&
                 val[2] == RID_ODID_OUI_2) ||
                (val[0] == 0x90 && val[1] == 0x3A && val[2] == 0xE6)) {
                if (parse_vendor_odid(val, elen, result)) {
                    any = true;
                }
            }
        }
        offset += 2u + elen;
    }
    return any;
}

bool rid_odid_parse_wifi_nan(const uint8_t *frame, size_t len, rid_odid_result_t *result)
{
    if (!frame || !result || len < 36) {
        return false;
    }
    // 目的地址在 offset 4
    static const uint8_t nan_dest[6] = {
        RID_ODID_NAN_DEST_0, RID_ODID_NAN_DEST_1, RID_ODID_NAN_DEST_2,
        RID_ODID_NAN_DEST_3, RID_ODID_NAN_DEST_4, RID_ODID_NAN_DEST_5,
    };
    if (memcmp(&frame[4], nan_dest, 6) != 0) {
        return false;
    }
    // NAN Action Frame 中查找 vendor-specific ODID
    // 简化：在整帧中搜索 OUI fa:0b:bc
    for (size_t i = 24; i + 4 < len; i++) {
        if (frame[i] == RID_ODID_OUI_0 && frame[i + 1] == RID_ODID_OUI_1 &&
            frame[i + 2] == RID_ODID_OUI_2) {
            size_t remain = len - i;
            if (parse_vendor_odid(&frame[i], remain > 32 ? 32 : remain, result)) {
                return true;
            }
        }
    }
    return false;
}

bool rid_odid_parse_ble_adv(const uint8_t *adv, size_t len, rid_odid_result_t *result)
{
    if (!adv || !result || len < 8) {
        return false;
    }
    // 遍历 AD structure：len | type | data
    size_t i = 0;
    bool any = false;
    while (i + 2 <= len) {
        uint8_t adv_len = adv[i];
        if (adv_len == 0) {
            break;
        }
        if (i + 1 + adv_len > len) {
            break;
        }
        uint8_t ad_type = adv[i + 1];
        const uint8_t *data = &adv[i + 2];
        uint8_t dlen = (uint8_t)(adv_len - 1);
        // Service Data 16-bit UUID = 0x16
        if (ad_type == 0x16 && dlen >= 3 &&
            data[0] == RID_ODID_BLE_SERVICE_LSB && data[1] == RID_ODID_BLE_SERVICE_MSB) {
            const uint8_t *payload = &data[2];
            size_t plen = dlen - 2;
            if (plen >= 1 && (payload[0] >> 4) == ODID_MSG_PACK) {
                if (rid_odid_parse_pack(payload, plen, result)) {
                    any = true;
                }
            } else if (plen >= RID_ODID_MSG_SIZE) {
                if (rid_odid_parse_message(payload, result)) {
                    any = true;
                }
            }
        }
        i += 1u + adv_len;
    }
    return any;
}

void rid_odid_apply_to_store(rid_store_t *store, const uint8_t mac[6],
                             const rid_odid_result_t *result, rid_link_t link,
                             int8_t rssi, uint32_t now_ms)
{
    if (!store || !mac || !result) {
        return;
    }
    rid_uav_t *uav = rid_store_touch(store, mac, now_ms);
    if (!uav) {
        return;
    }
    if (result->has_basic) {
        rid_store_merge_basic(uav, result->uas_id);
    }
    if (result->has_location) {
        rid_store_merge_location(uav, result->status, result->lat, result->lon,
                                 result->alt_m, result->height_m, result->speed_mps,
                                 result->heading_deg);
    }
    if (result->has_operator) {
        rid_store_merge_operator(uav, result->op_id);
    }
    if (result->has_system) {
        rid_store_merge_system(uav, result->op_lat, result->op_lon);
    }
    rid_store_mark_hit(store, uav, link, rssi, now_ms);
}

const char *rid_odid_status_text(rid_status_t status)
{
    switch (status) {
    case RID_STATUS_GROUND:
        return "地面";
    case RID_STATUS_AIRBORNE:
        return "飞行";
    case RID_STATUS_EMERGENCY:
        return "紧急";
    case RID_STATUS_FAIL:
        return "故障";
    default:
        return "未知";
    }
}

const char *rid_odid_link_text(rid_link_t link)
{
    switch (link) {
    case RID_LINK_WIFI_BEACON:
        return "WiFi";
    case RID_LINK_WIFI_NAN:
        return "NAN";
    case RID_LINK_BLE:
        return "BLE";
    default:
        return "-";
    }
}
