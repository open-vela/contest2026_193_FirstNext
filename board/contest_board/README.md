# Contest Board — R528S3（DshanPI）板级适配

本目录包含喵喵机在 r528s3-dshanpi 开发板（全志 R528S3，320×480 SPI LCD + 电容触摸 + SPI NAND）上的**全部板级适配改动**，覆盖 openvela 工作区中的 `nuttx` 与 `vendor/allwinnertech` 两个源码仓。文件按 `<源码仓>/<原始路径>` 镜像存放。

## 适配内容

| 文件（相对源码仓） | 说明 |
|---|---|
| `nuttx/drivers/video/spi_lcd_fb.c` | **新增**：SPI LCD 帧缓冲驱动，将 SPI 屏接入标准 framebuffer（`/dev/fb0`） |
| `nuttx/drivers/video/{Kconfig,Make.defs,CMakeLists.txt}` | 帧缓冲驱动接入 NuttX 构建系统 |
| `nuttx/drivers/input/ft5x06.c` | 电容触摸控制器（FT5x06）I2C 驱动适配 |
| `nuttx/fs/yaffs/{Kconfig,CMakeLists.txt,yaffs_vfs.c}` | YAFFS 文件系统修复（`/data` 分区断电可靠性与挂载） |
| `vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/r528_bringup.c` | 板级初始化：串口 pinmux 早期化修复（修复 boot 早期串口哑死） |
| `vendor/allwinnertech/chips/r528/r528_boot.c` | 启动流程适配 |
| `vendor/allwinnertech/boards/r528/r528s3-dshanpi/configs/nsh/defconfig` | 板级配置（LVGL、字体、WiFi、NTP、YAFFS 等） |
| `vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/*.bin` | **新增**：6 档 LVGL 字库（5 档中文 + IPA 音标专用，PLAIN 格式），由 `tools/gen_board_fonts.js` 生成，合计约 3.5MB，覆盖 3158 字符 + GB2312 常用字 |
| `vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/init.d/rcS` | 系统启动脚本：挂载 `/data`、拉起喵喵机应用 |
| `vendor/allwinnertech/apps/wifi_manager/Makefile` | WiFi 管理守护的构建接入 |

## 应用方式

`repo sync` 拉取完整工程后、编译前执行一次：

```bash
bash contest2026_193_FirstNext/board/contest_board/apply_port.sh <openvela工作区根目录>
```

脚本会把上述文件复制到工作区对应位置，并移除被新字库取代的旧字体文件。

## 字库再生成

修改 UI 文案或词库后：

```bash
python3 tools/gen_words.py            # 可选: 重新生成词库
node tools/gen_board_fonts.js         # 重新生成字库
bash board/contest_board/apply_port.sh <工作区>   # 重新应用
```

详细设计见 `docs/喵喵机方案.md`。
