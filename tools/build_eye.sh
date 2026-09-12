#!/bin/bash
# 编译 + 打包 Team193 眼睛应用到 R528 板子
# 注意: 非交互 shell 不加载 .bashrc, 必须显式注入交叉工具链 PATH
TREE=/home/mfskys/openvela_contest

export PATH=$TREE/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$TREE/prebuilts/tools/linux/x86_64:$PATH
export PATH=$TREE/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PATH

echo "=== [$0] start $(date) ==="
echo "=== toolchain check ==="
which arm-none-eabi-gcc || echo "!! TOOLCHAIN MISSING"
arm-none-eabi-gcc --version 2>&1 | head -1

cd "$TREE/vendor/allwinnertech/lichee" || { echo "LICHEE DIR NOT FOUND"; exit 1; }

source vela_env.sh
source envsetup.sh

echo "=== lunch ==="
lunch_nuttx r528s3-dshanpi
echo "LUNCH_EXIT=$?"

echo "=== remove stale nuttx .config (force re-config from defconfig) ==="
rm -f "$TREE/nuttx/.config"

echo "=== build (m) ==="
m 2>&1 | tee /tmp/build_eye.log
echo "BUILD_EXIT=${PIPESTATUS[0]}"

# 护栏: 编译失败则中止, 不再 pack
# (否则 pack 会沿用旧 nsh.fex, 产出"构建成功但代码没变"的假镜像)
if grep -qE "error:|Error:|Error [0-9]|build .* fail" /tmp/build_eye.log; then
  echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  echo "!! BUILD FAILED -- 中止 pack, 判定为失败"
  echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  grep -nE "error:|Error:|Error [0-9]" /tmp/build_eye.log | head -20
  exit 1
fi

echo "=== app 编译产物校验 ==="
APP="$TREE/packages/demos/contest2026_193_hello_app"
if [ -d "$APP" ]; then
  n=$(ls "$APP"/*.o 2>/dev/null | wc -l)
  if [ "$n" -gt 0 ]; then
    echo "  OK: 应用已编译($n 个 .o)"
  else
    echo "  !! 警告: 未找到 .o, 应用可能未参与本次编译(检查 CONFIG_LVX_USE_DEMO_CONTEST2026_193_EYE)"
  fi
fi

echo "=== eye_app artifacts check ==="
find "$TREE" -name 'eye_app*' 2>/dev/null | head -5
ls "$TREE/apps/builtin/registry/" 2>/dev/null | grep -i eye
grep -ri "eye_app" "$TREE/apps/builtin/registry/"*.bdat 2>/dev/null | head -3

echo "=== pack ==="
pack
echo "PACK_EXIT=$?"

echo "=== output images ==="
ls -l "$TREE/vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/"*.img 2>&1

echo "=== [$0] end $(date) ==="
