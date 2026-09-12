---
name: openvela-image-verify
description: openvela/NuttX 固件镜像的"真成功"验证与真机问题定位。当构建完成后需要确认代码真正进入固件、真机行为与代码不符、怀疑增量编译未生效、需要从串口日志定位 LVGL/驱动问题、或构建报错但仍产出镜像时使用。
---

# openvela 固件镜像防"假成功"验证工作流

构建"成功"不等于代码进了固件。本流程用三层证据闭环确认：编译产物 → 链接产物 → 镜像内容。

## 核心坑：build.sh 的"假成功"

openvela 的 `build.sh` 在应用编译失败时**仍会继续 pack**，产出"构建成功但代码没变"的镜像。护栏：

```bash
m 2>&1 | tee /tmp/build.log
if grep -qE "error:|Error:|Error [0-9]|build .* fail" /tmp/build.log; then
  echo "BUILD FAILED -- 中止 pack"
  exit 1
fi
pack   # 只有编译干净才打包
```

## 三层验证

### 第 1 层：编译产物时间戳

```bash
# .o 必须晚于最后一次源码修改
ls -la --time-style=full-iso <app_dir>/*.o <app_dir>/*.c
```

典型陷阱：
- **Kconfig 改动不触发源文件重编**：改了 `CONFIG_XXX` 但依赖它的 `.o` 时间戳更旧 → `touch` 源文件或清理重编
- **配置只进了 .config 没进代码**：`.config`/`config.h` 是新的，链接的还是旧 `.o`

### 第 2 层：链接产物字符串

```bash
# 最终链接的内核里检索本次新增的字符串(日志文案最有效)
strings nuttx/nuttx | grep "新增的唯一日志文案"
# 或直接对镜像二进制检索
grep -c "新增文案" firmware.img
```

`grep -c` 返回 >0 = 代码在镜像里。这是最可靠的铁证。

### 第 3 层：镜像体积差

对照本次改动的预期体积：如替换字体后镜像应增大 ≈ 新旧字体体积差。数字对不上 = 中间环节丢失。

## 真机串口日志定位

| 日志特征 | 含义 | 方向 |
|---|---|---|
| `lv_font_get_bitmap_fmt_txt: Compressed fonts...` | 字库压缩与 LVGL 配置不匹配 | 换 PLAIN 字库或开解压 |
| `[mmj] fonts 12=0x... 16=0x...` 指针非空 | 字库加载成功 | 字体链路 OK |
| `[mmj] fonts ... 21=(nil)` | 某 bin 加载失败 | 检查文件名/路径/ETC 打包 |
| boot 停在 `Skip DISP2` 无后续 | 早期串口 pinmux 被抢占 | 板级 bringup 早期化 pinmux |
| `ntp start failed` / 守护线程缺失 | daemon 线程创建失败(内存紧张) | 改用应用内直连(单次 UDP socket) |
| 时间对但差 8 小时 | TZ 未设 | `setenv("TZ","CST-8",1); tzset();` |
| HTTP 兜底无输出文件 | 重定向到不存在的目录 | NuttX 无 /tmp, 用 /data |

## 外部依赖降级设计

真机上任何"联网增强"功能（NTP、TTS、OTA）都可能失败，设计原则：
1. **主链路离线可用**——联网功能失败时静默降级，绝不阻塞 UI
2. **多路径兜底**——NTP(UDP) 失败 → HTTP Date(TCP)；域名失败 → 固定 IP
3. **每条路径留日志**——串口一行输出定位卡在哪层
4. **时区显式设置**——对时写 UTC epoch，`localtime()` 依赖 TZ 环境变量

## 验证清单（烧录前过一遍）

- [ ] 构建日志无 `error:`（护栏通过）
- [ ] 应用 `.o` 时间戳 > 源码修改时间
- [ ] `nuttx/nuttx` 中能 grep 到本次新增文案
- [ ] 镜像体积变化与改动量吻合
- [ ] 板级 ETC 缓存（`etctmp*`）已清理
