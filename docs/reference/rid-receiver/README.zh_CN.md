# FoloRID — 开源 ESP32 Remote ID 接收器

[English](README.md) | [简体中文](README.zh_CN.md)

基于 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) 的开源无人机**远程识别（Remote ID）接收与解析**应用。

## 功能

- 扫描并解析 **OpenDroneID / ASTM F3411** 广播
  - WiFi Beacon 厂商 IE（OUI `FA:0B:BC` / Parrot `90:3A:E6`）
  - WiFi NAN 行动帧消息包
  - BLE Service Data（UUID `0xFFFA`）
- 中文值守终端 UI（深色 ATC 风格，非 demo 菜单）
- 目标列表 + 单机详情（ID、操作员、经纬度、高度、速度、航向、状态、RSSI）
- 三键交互：UP/DOWN 选择，OK 进详情，长按暂停/恢复扫描
- 右上角电量显示（`bsp_battery_soc()`，不可用时优雅降级）

## 硬件

| 项目 | 说明 |
| --- | --- |
| 芯片 | ESP32-C3 |
| Flash | 8 MB |
| 屏幕 | ST7789 240×320 SPI |
| 按键 | UP / DOWN / OK（GPIO0 ADC 分压） |
| 射频 | 2.4 GHz WiFi + BLE（仅监听，不发射 RID） |

## 构建

需要 **ESP-IDF v5.5.3**。

```bash
source <esp-idf>/export.sh   # Windows: export.bat
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin
```

从 **0x0** 烧录合并固件：

```bash
python -m esptool --chip esp32c3 -p <PORT> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

## 免责声明

本应用仅**接收**公开 Remote ID 广播，用于开源学习、合规检查与空域态势感知。不提供破解、欺骗或规避 RID 的能力。实际接收效果取决于环境、机型与法规要求，编译通过不代表真机验收通过。
