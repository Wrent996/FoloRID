# FoloRID — 开源 ESP32 Remote ID 接收器

[English](README.en.md) | [简体中文](README.zh_CN.md)

基于 **FoloToy AI Passport**（ESP32-C3）的开源无人机 **Remote ID（远程识别）接收与解析**应用。

可扫描并解析 **OpenDroneID / ASTM F3411** 广播，在 240×320 屏幕上以中文「空域值守」终端界面展示目标列表与详情。

> 协议参考：[PeterJBurke/RID](https://github.com/PeterJBurke/RID) · [opendroneid/opendroneid-core-c](https://github.com/opendroneid/opendroneid-core-c) · 硬件基线：[FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport)

## 功能

- **WiFi Beacon**：厂商 IE OUI `FA:0B:BC`（OpenDroneID）、`90:3A:E6`（Parrot）
- **WiFi NAN**：行动帧消息包
- **BLE**：Service Data UUID `0xFFFA`
- 中文 UI：目标列表 + 单机详情（ID / 操作员 / 经纬度 / 高度 / 速度 / 航向 / 状态 / RSSI）
- 三键：`UP/DOWN` 选择 · `OK` 详情 · `OK` 长按暂停/恢复
- 右上角电量显示（读失败时优雅降级）
- **只接收、不发射** RID

## 硬件

| 项目 | 规格 |
| --- | --- |
| 芯片 | ESP32-C3 |
| Flash | 8 MB（无 PSRAM） |
| 屏幕 | ST7789 240×320 SPI |
| 按键 | UP / DOWN / OK（GPIO0 ADC 分压） |
| 射频 | 2.4 GHz WiFi + BLE |

引脚以 `components/bsp/include/bsp_pins.h` 为准。

## 快速刷写（从 0x0）

已附带合并固件（含 bootloader + 分区表 + 应用）：

```bash
python -m esptool --chip esp32c3 -p <PORT> -b 460800 \
  write-flash 0x0 build/FoloRID-full.bin
```

合并镜像路径：

```text
build/FoloRID-full.bin
```

> 烧录会覆盖原固件；可能重置 NVS/PHY。刷写前请自行确认无需保留的数据。

## 从源码构建

需要 **ESP-IDF v5.5.3**（ESP32-C3）。

```bash
export.sh          # Windows: export.bat
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/FoloRID-full.bin
```

主机逻辑测试：

```bash
gcc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_rid_store.c main/rid_store.c main/rid_odid.c -lm \
  -o /tmp/test_rid_store && /tmp/test_rid_store
```

## 项目结构

```text
components/bsp/     板级支持（显示/按键/电池/I2C，复用自 ai-passport）
main/               RID 应用：协议解析、目标库、射频扫描、中文 UI
assets/fonts/       思源黑体 SC 子集 rid_font_16 + 转换说明
tests/              可 host 运行的协议/目标库测试
tools/verify_firmware.py   合并镜像布局校验
build/FoloRID-full.bin     可从 0x0 刷写的合并固件
```

## 按键与界面

| 按键 | 列表页 | 详情页 |
| --- | --- | --- |
| UP / DOWN | 上下选择目标 | 返回列表 |
| OK 单击 | 进入详情 | 返回列表 |
| OK 长按 | 暂停 / 恢复扫描 | 返回列表 |

## 授权与声明

- 应用代码：MIT（见 `LICENSE`）
- 中文字库源：Source Han Sans SC，SIL OFL 1.1
- 协议实现参考 OpenDroneID / ASTM F3411 公开规范与开源接收端实现

**免责声明**：本项目仅用于开源学习、合规检查与空域态势感知的**接收**用途，不提供破解、欺骗或规避 Remote ID 的能力。编译通过 ≠ 真机验收通过；接收效果取决于环境、机型与当地法规。

## 致谢

- [FoloToy ai-passport](https://gitee.com/FoloToy/ai-passport) — 硬件 BSP 与开发基线
- [PeterJBurke/RID](https://github.com/PeterJBurke/RID) — ESP32 RID 接收参考
- [OpenDroneID](https://github.com/opendroneid) — 协议与 C 库
