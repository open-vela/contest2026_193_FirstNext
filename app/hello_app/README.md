# 喵喵机 MMJ — 板端英语学习机（R528 / NuttX）

通过 manifest `<linkfile>` 映射到 openvela `packages/demos/contest2026_193_hello_app`，
构建产物 **`eye_app`**，由系统启动脚本开机拉起。

## 模块说明

- `mmj_main.c` — 主程序。LVGL 板端接入（`/dev/fb0` + `/dev/input0`），
  五个页面（主页 / 学习 / 闯关 / 词库 / 设置）、分级间隔复习调度、
  流式 JSON 进度存储（`/data/mmj/`）、WiFi 扫描连接、NTP/HTTP 自动对时、三主题换肤。
- `mmj_words.c / .h` — CET-4 词库 7508 词（单词 / 英美音标 / 词性 / 释义 / 例句），
  由 `tools/gen_words.py` 生成，**勿手改**。

## 开发约定

- 修改词库或新增中文文案后，必须重跑 `tools/gen_board_fonts.js` 更新字库
  （脚本会自动提取全部用字并清理板级 ETC romfs 缓存）。
- 进度文件格式版本见 `mmj_main.c` 中 `words.json` 的 `"v":2` 头部校验。

详细设计见 `docs/喵喵机方案.md`。
