# Assets

[English](README.md) | [简体中文](README.zh_CN.md)

## fonts/

| 文件 | 说明 |
| --- | --- |
| `SourceHanSansSC-Regular.otf` | 思源黑体 SC Regular（Source Han Sans，SIL OFL 1.1）。字体转换输入源，不参与固件链接。 |
| `rid_font_16.c` | 由 `lv_font_conv` 生成的 RID 应用中文字库子集（16px，bpp=4，LVGL 9.x）。符号：`rid_font_16`。 |

### 生成命令

```bash
lv_font_conv \
  --font assets/fonts/SourceHanSansSC-Regular.otf \
  --symbols "<应用固定中文文案+ASCII+数字+标点>" \
  --size 16 --bpp 4 --format lvgl --no-compress \
  --lv-font-name rid_font_16 --lv-include lvgl.h \
  --output assets/fonts/rid_font_16.c
```

- 工具：`lv_font_conv`（npm），LVGL 目标版本 9.5.0
- 授权：Source Han Sans SC 为 SIL Open Font License 1.1
- 字符范围：应用固定 UI 文案所用简体中文 + 可打印 ASCII + 常用标点。动态内容（任意无人机 ID 中的生僻字）不在子集内，超出时由 Montserrat 14 fallback 或占位符显示。

