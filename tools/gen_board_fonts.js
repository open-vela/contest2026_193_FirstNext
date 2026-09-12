/*
 * 板端中文字体生成 · 喵喵机 MMJ
 *
 *   - 字符集: 从板端 C 源码(UI 文案/词库音标/中文释义)自动提取
 *   - 源字体: 阿里巴巴普惠体
 *   - 输出  : LVGL bin 字体, 板端 lv_binfont_create() 运行时加载
 *
 * 用法(WSL):  node tools/gen_board_fonts.js
 * 新增中文文案或词库后必须重新生成, 否则新字(含 IPA 音标)显示豆腐块。
 */
const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const REPO = '/home/mfskys/openvela_contest/contest2026_193_FirstNext';
const SRC_DIR = REPO + '/app/hello_app';
const TREE = '/home/mfskys/openvela_contest';
const TTF_REG =
  TREE + '/packages/demos/bandx/resource/font/assets/AlibabaPuHuiTi-3-55-Regular.ttf';
const TTF_BOLD =
  TREE + '/packages/demos/bandx/resource/font/assets/AlibabaPuHuiTi-3-95-ExtraBold.ttf';
const TTF_MISANS =
  TREE + '/vendor/allwinnertech/lichee/board/common/data/res/fonts/MiSans-Normal.ttf';
/* DejaVuSans(自由许可): 实测含 15/18 个 IPA 音标字形, 与 MiSans(补 ŋ ð ɑ)合并后 18/18 全覆盖 */
const TTF_DEJAVU = __dirname + '/fonts/DejaVuSans.ttf';
const OUT_DIR =
  TREE + '/vendor/allwinnertech/boards/r528/r528s3-dshanpi/src/etc/fonts';

const chars = new Set();
const addStr = (s) => {
  for (const ch of s) if (ch.codePointAt(0) > 0x7f) chars.add(ch);
};

/* 1) 板端 C 源码: UI 文案 + 词库音标(IPA) + 中文释义 */
for (const f of fs.readdirSync(SRC_DIR)) {
  if (!/\.(c|h)$/.test(f)) continue;
  const txt = fs.readFileSync(path.join(SRC_DIR, f), 'utf8');
  for (const m of txt.match(/"([^"\n]*)"/g) || []) addStr(m.slice(1, -1));
}

/* 3) 常用标点与符号 */
addStr('：；，。！？·×÷％-—…、（）「」『』【】《》￥＋℃°↑↓←→＊#♪信号强弱已等待中');

/* 3.4) 精简集固化(GB2312 并入之前): UI 文案 + 词库 + IPA + 符号
 *      —— 21/28/16b 档用, 词库中文必须全在, 不能丢 */
const base_syms = [...chars].sort().join('');
console.log('base chars:', chars.size);

/* 3.5) GB2312 全部汉字(SSID 是任意中文, 必须全量覆盖; 仅 12/16px 两档并入) */
const CN_FILE = __dirname + '/font_cn_gb2312.txt';
let cn_all = '';
if (fs.existsSync(CN_FILE)) {
  cn_all = fs.readFileSync(CN_FILE, 'utf8').replace(/\s/g, '');
  for (const ch of cn_all) chars.add(ch);
  console.log('GB2312 chars:', cn_all.length);
}

const symbols = [...chars].sort().join('');                    /* 全量: 含 GB2312 */
console.log('unique chars:', chars.size);

/* 4) 生成多档(覆盖设计字号: 小字12 / 正文16 / 释义21 / 大标题28)
 *    12/16px 用全量字符集(含 GB2312, 支持任意中文 SSID); 21/28/16b 用精简集 */
const jobs = [
  [TTF_REG, 12, 'font_puhui_12_4.bin', symbols],
  [TTF_REG, 16, 'font_puhui_16_4.bin', symbols],
  [TTF_REG, 21, 'font_puhui_21_4.bin', base_syms],
  [TTF_REG, 28, 'font_puhui_28_4.bin', base_syms],
  [TTF_BOLD, 16, 'font_puhui_16b_4.bin', base_syms],
];

for (const [ttf, size, name, sym] of jobs) {
  const out = path.join(OUT_DIR, name);
  const r = spawnSync(
    'npx',
    [
      '--yes', 'lv_font_conv',
      '--bpp', '4',
      '--size', String(size),
      '--font', ttf,
      '-r', '0x20-0x7F',
      '--symbols', sym,
      '--format', 'bin',
      '--no-compress',   /* PLAIN 位图: 走 LVGL 非压缩分支, 不依赖 LV_USE_FONT_COMPRESSED */
      '-o', out,
    ],
    { stdio: 'inherit' }
  );
  if (r.status !== 0) {
    console.error('FAILED:', name);
    process.exit(1);
  }
  console.log('generated', name, fs.statSync(out).size, 'bytes');
}

/* 6) 音标专用字体(IPA):
 *    阿里普惠体与 MiSans 实测均缺大部分 IPA 字形(lv_font_conv 对缺字形的字符静默跳过,
 *    导致板端音标显示方块)。DejaVuSans 含 15/18 个 IPA, 再由 MiSans 补齐
 *    ŋ/ð/ɑ 三个, 合并后 18/18 全覆盖(生成后必须用解析器复核)。
 *    字符集 = 源码中出现的非 CJK 符号(拉丁扩展/IPA/重音), 码点 < 0x2E80。
 */
{
  let ipa = '';
  for (const ch of [...chars].sort()) {
    const cp = ch.codePointAt(0);
    if (cp >= 0x80 && cp < 0x2e80) ipa += ch;
  }
  ipa += 'ˈˌː·';                      /* 音标重音/长音/中点兜底 */
  console.log('ipa charset:', ipa.length, 'chars');

  const out = path.join(OUT_DIR, 'font_ipa_16_4.bin');
  const r = spawnSync(
    'npx',
    [
      '--yes', 'lv_font_conv',
      '--bpp', '4',
      '--size', '16',
      /* 多字体: DejaVu 出 IPA 主体(15/18)+ASCII, MiSans 只补 DejaVu 缺的 ŋ ð ɑ */
      '--font', TTF_DEJAVU, '-r', '0x20-0x7F', '--symbols', ipa,
      '--font', TTF_MISANS, '--symbols', 'ŋðɑ',
      '--format', 'bin',
      '--no-compress',
      '-o', out,
    ],
    { stdio: 'inherit' }
  );
  if (r.status !== 0) {
    console.error('FAILED: font_ipa_16_4.bin');
    process.exit(1);
  }
  console.log('generated font_ipa_16_4.bin', fs.statSync(out).size, 'bytes');
}

/* 5) 使 NuttX ETC romfs 缓存失效
 *
 * nuttx/boards/Board.mk 里 etctmp.c 的依赖是 src/etc 目录本身(不是目录内文件),
 * 覆盖同名 .bin 不会改变目录 mtime, 因此 etctmp.c 不会重建, 新字体进不了固件。
 * 这里显式删除, 强制下次编译重新拷目录 + 重跑 genromfs/xxd。
 */
const BOARD_SRC = path.resolve(OUT_DIR, '..', '..');        // .../r528s3-dshanpi/src
const ETCTMP_C = path.join(BOARD_SRC, 'etctmp.c');
const ETCTMP_D = path.join(BOARD_SRC, 'etctmp');
for (const p of [ETCTMP_C, ETCTMP_D]) {
  if (fs.existsSync(p)) {
    spawnSync('rm', ['-rf', p]);        /* node12 兼容: 用 rm 而非 fs.rmSync */
    console.log('cache invalidated:', p);
  }
}
