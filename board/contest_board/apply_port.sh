#!/bin/bash
# 将本目录中的板级适配文件应用到 openvela 工作区(构建前执行一次)。
# 用法: bash apply_port.sh <openvela工作区根目录>
TREE="${1:-$(dirname "$(pwd)")}"
PORT="$(cd "$(dirname "$0")/r528s3_port" && pwd)"

if [ ! -d "$TREE/nuttx" ]; then echo "错误: $TREE 不是 openvela 工作区"; exit 1; fi

set -e
cp -v "$PORT/nuttx/drivers/input/Make.defs" "$TREE/nuttx/drivers/input/Make.defs"
cp -v "$PORT/nuttx/drivers/input/ft5x06.c" "$TREE/nuttx/drivers/input/ft5x06.c"
cp -v "$PORT/nuttx/drivers/video/CMakeLists.txt" "$TREE/nuttx/drivers/video/CMakeLists.txt"
cp -v "$PORT/nuttx/drivers/video/Kconfig" "$TREE/nuttx/drivers/video/Kconfig"
cp -v "$PORT/nuttx/drivers/video/Make.defs" "$TREE/nuttx/drivers/video/Make.defs"
cp -v "$PORT/nuttx/drivers/video/spi_lcd_fb.c" "$TREE/nuttx/drivers/video/spi_lcd_fb.c"
cp -v "$PORT/nuttx/fs/yaffs/CMakeLists.txt" "$TREE/nuttx/fs/yaffs/CMakeLists.txt"
cp -v "$PORT/nuttx/fs/yaffs/Kconfig" "$TREE/nuttx/fs/yaffs/Kconfig"
cp -v "$PORT/nuttx/fs/yaffs/yaffs_vfs.c" "$TREE/nuttx/fs/yaffs/yaffs_vfs.c"
cp -v "$PORT/vendor/allwinnertech/apps/wifi_manager/Makefile" "$TREE/vendor/allwinnertech/apps/wifi_manager/Makefile"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/configs/nsh/defconfig" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/configs/nsh/defconfig"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/Makefile" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/Makefile"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/init.d/rcS" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/init.d/rcS"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/r528_bringup.c" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/r528_bringup.c"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_12_4.bin" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_12_4.bin"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_16_4.bin" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_16_4.bin"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_16b_4.bin" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_16b_4.bin"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_21_4.bin" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_21_4.bin"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_28_4.bin" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_28_4.bin"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_ipa_16_4.bin" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_ipa_16_4.bin"
cp -v "$PORT/vendor/allwinnertech/chips/r528/r528_boot.c" "$TREE/vendor/allwinnertech/chips/r528/r528_boot.c"
cp -v "$PORT/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/web/admin.html" "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/web/admin.html"
rm -fv "$TREE/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts/font_puhui_20_4.bin"

echo "板级适配文件应用完成。"
