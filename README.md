# FoloRID — Open-source ESP32 Remote ID Receiver

[English](README.md) | [简体中文](README.zh_CN.md)

Open-source **Remote ID receive & parse** application for the **FoloToy AI Passport** (ESP32-C3).

Scans and parses **OpenDroneID / ASTM F3411** broadcasts and shows UAV list/details on the 240×320 display with a Chinese airspace-watch UI.

> Protocol refs: [PeterJBurke/RID](https://github.com/PeterJBurke/RID) · [opendroneid-core-c](https://github.com/opendroneid/opendroneid-core-c) · Hardware baseline: [FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport)

## Features

- **WiFi Beacon** vendor IEs: OUI `FA:0B:BC` (OpenDroneID), `90:3A:E6` (Parrot)
- **WiFi NAN** action-frame message packs
- **BLE** Service Data UUID `0xFFFA`
- Chinese UI: UAV list + detail (ID / operator / lat-lon / altitude / speed / heading / status / RSSI)
- Keys: `UP/DOWN` select · `OK` detail · long-press `OK` pause/resume
- Battery badge top-right (graceful fallback when unavailable)
- **Receive-only** — does not transmit RID

## Hardware

| Item | Spec |
| --- | --- |
| MCU | ESP32-C3 |
| Flash | 8 MB (no PSRAM) |
| Display | ST7789 240×320 SPI |
| Keys | UP / DOWN / OK (GPIO0 ADC divider) |
| Radio | 2.4 GHz Wi-Fi + BLE |

Pinout source of truth: `components/bsp/include/bsp_pins.h`.

## Flash from 0x0

Prebuilt merged image (bootloader + partition table + app):

```bash
python -m esptool --chip esp32c3 -p <PORT> -b 460800 \
  write-flash 0x0 build/FoloRID-full.bin
```

Path: `build/FoloRID-full.bin`

> Flashing overwrites existing firmware and may reset NVS/PHY. Export anything you need first.

## Build from source

Requires **ESP-IDF v5.5.3** (target `esp32c3`).

```bash
export.sh          # Windows: export.bat
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/FoloRID-full.bin
```

Host logic tests:

```bash
gcc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_rid_store.c main/rid_store.c main/rid_odid.c -lm \
  -o /tmp/test_rid_store && /tmp/test_rid_store
```

## Layout

```text
components/bsp/     Board support (display/keys/battery/I2C, from ai-passport)
main/               RID app: decode, store, radio scan, Chinese UI
assets/fonts/       Source Han Sans SC subset rid_font_16
tests/              Host-runnable protocol/store tests
build/FoloRID-full.bin   Merged image flashable at 0x0
```

## Keys

| Key | List page | Detail page |
| --- | --- | --- |
| UP / DOWN | Move selection | Back to list |
| OK click | Open detail | Back to list |
| OK long | Pause / resume scan | Back to list |

## License & disclaimer

- App code: MIT (see `LICENSE`)
- Font source: Source Han Sans SC (SIL OFL 1.1)
- Protocol work based on public OpenDroneID / ASTM F3411 and open receivers

**Disclaimer**: receive-only educational / compliance / situational-awareness tool. No RID spoofing, cracking, or evasion. A successful build is not hardware validation; RF results depend on environment and local regulations.

## Credits

- [FoloToy ai-passport](https://gitee.com/FoloToy/ai-passport) — BSP baseline
- [PeterJBurke/RID](https://github.com/PeterJBurke/RID) — ESP32 RID receiver reference
- [OpenDroneID](https://github.com/opendroneid) — protocol & C library
