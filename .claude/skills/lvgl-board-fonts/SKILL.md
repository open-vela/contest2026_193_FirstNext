---
name: lvgl-board-fonts
description: 为 openvela/NuttX 板端 LVGL 应用生成与应用中文及 IPA 音标字库。当需要为嵌入式 LVGL 项目生成字体 bin、板端中文显示为方块（豆腐块）、新增中文文案后需要更新字库、音标符号显示不全、或需要让自定义字库进入固件镜像时使用。
---

# openvela 板端 LVGL 中文字库生成与应用

在 openvela（NuttX）板端 LVGL 应用中正确生成、部署中文字库与 IPA 音标字库的完整流程。每一步都来自真实真机踩坑。

## 核心原则

1. **字库必须显式进入固件**：板级 `src/etc/` 目录通过 `Board.mk` 打包成 C 数组（`etctmp.c`）编译进固件，打包目标依赖的是**目录 mtime**，不是目录内文件——覆盖同名字体文件**不会触发重建**，新字库根本进不了固件（真机反复显示旧字体的根因）。
2. **PLAIN 格式优先**：`lv_font_conv` 默认开启 RLE 压缩，而板端 LVGL 需要 `CONFIG_LV_USE_FONT_COMPRESSED=y` 且**必须重编 LVGL 源文件**才生效（增量编译常不重编 → 运行时刷 `Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled` 警告、字形取不到、显示方块）。用 `--no-compress` 生成 PLAIN 格式可完全绕开该配置依赖。

## 工作流

### 第 1 步：提取实际用到的字符集

从**全部**会显示中文/符号的来源提取，漏一处就缺字（方块）：

- 应用 C 源码中的字符串字面量（UI 文案、状态文本）
- 词库/数据源文件（中文释义、IPA 音标：`ˈ ˌ ː ə ð ŋ ɑ ɛ ɪ ʃ ʊ θ ʒ ɔ ˑ` 等）
- 常用中文标点兜底：`：；，。！？·×÷％-—…、（）「」『』【】《》￥＋℃°↑↓←→＊#`
- 若需要显示任意中文输入（如 WiFi SSID 列表），须并入 **GB2312 全量汉字**（约 6763 字），单独一两个字号的字库承担

### 第 2 步：多档生成

```bash
npx --yes lv_font_conv \
  --bpp 4 --size 16 \
  --font AlibabaPuHuiTi-Regular.ttf \
  -r 0x20-0x7F \
  --symbols "$(cat charset.txt)" \
  --format bin \
  --no-compress \
  -o font_cn_16_4.bin
```

要点：
- `--no-compress` 必加（PLAIN 位图，走 LVGL 非压缩分支）
- 按设计稿字号出多档（如 12/16/16b 粗体/21/28），小字号承担全量字符集，大字号用精简集控制体积
- 生成后**校验格式**：解析 bin 头部（偏移 41 的 `compression_id` 字段）确认 `0`（PLAIN）

### 第 3 步：IPA 音标专项

阿里普惠体、MiSans 实测均缺大部分 IPA 字形，且 `lv_font_conv` 对缺字形**静默跳过**（不报错！），生成后音标必现方块。处理：

- 用含完整 IPA 的自由许可字体（如 DejaVuSans，含 15/18 个常用 IPA）为主
- 缺失的 `ŋ ð ɑ` 由 MiSans 补齐，多字体合并生成
- 生成后用 bin 解析器复核字符覆盖数（如 18/18）

### 第 4 步：部署 + 强制缓存失效

```bash
# 1) 字库放入板级 etc 目录
cp font_*.bin  <vendor>/boards/<board>/src/etc/fonts/

# 2) 关键: 删除 ETC romfs 缓存, 强制重建
rm -rf <vendor>/boards/<board>/src/etctmp <vendor>/boards/<board>/src/etctmp.c
```

更稳妥的做法：把"删除 etctmp 缓存"写进字库生成脚本末尾，永不遗漏。

### 第 5 步：确认字库真正进了固件

```bash
# 对比 etctmp 中字体大小与源文件一致
ls -la <vendor>/boards/<board>/src/etctmp/etc/fonts/

# 铁证: 镜像增大值 ≈ 字库体积差; 或在镜像中检索新增字符串
grep -c "新增文案" firmware.img
```

### 第 6 步：板端运行时加载

```c
lv_font_t *f = lv_binfont_create("/etc/fonts/font_cn_16_4.bin");
if (f == NULL) printf("[app] font load failed\n");   /* 必须检查, 失败回退内置字体 */
```

## 踩坑速查表

| 症状 | 根因 | 解法 |
|---|---|---|
| 中文全部方块，刷 `Compressed fonts` 警告 | 压缩字库 + 板端未启用解压 | `--no-compress` 重生成 |
| 新字库生成后真机没变化 | `etctmp.c` 依赖目录 mtime，覆盖同名文件不重建 | 生成脚本末尾删 `etctmp*` |
| 镜像没变大 | 字库进了 `src/etc` 但打包源是 `board/common/data/res/`（另一条 romfs 路径） | 确认板级走的是 ETC→C 数组路径 |
| 音标个别方块 | 源字体缺 IPA 字形且 lv_font_conv 静默跳过 | 换 DejaVu 主字体 + 复核覆盖数 |
| 改了 `CONFIG_LV_USE_FONT_COMPRESSED` 不生效 | LVGL `.o` 未重编 | `touch` 相关源文件或清理重编 |
| WiFi 中文 SSID 乱码 | 精简字符集无该汉字 | 12/16px 档并入 GB2312 全量 |
