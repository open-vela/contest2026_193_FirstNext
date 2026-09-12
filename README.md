# 喵喵机 · 英语学习机（contest2026_193 FirstNext）

> 运行在 **openvela（NuttX RTOS）** + LVGL 上的**离线英语单词学习机**。
> 开机即进入学习界面，学习、词库、进度**全部本地运行**，断网可完整使用。

---

## 一、作品简介

**喵喵机**是一台为"背单词"这件事专门做的硬件：一块 320×480 的小屏、一个纯按键式界面，没有多余的东西。

它解决的是一件事——**让单词记得住**。核心是一条闭环：

```
学习(到期/逾期优先 → 错词重学 → 新词)
        │ 本单元全部词学过
        ▼
Unit 闯关(10 题 · 3 心 · 中→英四选一)   ← 答错的词当场再考一遍
        │
        ▼
错词回流 → 学习页优先复习 → 答对清错 → 重新闯关
```

配套一套**分级间隔复习**调度：每个单词独立记录等级，复习间隔按 **1 / 2 / 4 / 7 / 15 / 30 天**逐级拉长；答错立即降级回炉，连续两次答对才升级。首页直接显示「今日到期 / 已逾期」，用户不用自己想"今天该背什么"。

**亮点：**

1. **完全离线可用** —— 7508 词 CET-4 词库编译进固件，学习算法、学习记录全在板子本地（`/data/mmj/`），没网照样学。
2. **正统记忆曲线落地** —— 不是"随机抽词"，而是按每词自己的到期日调度，逾期词优先红标。
3. **当场纠错闭环** —— 闯关答错的词不会就这样过去，当局结束前必再考一遍；错 ≥3 次的词会被优先抽到。
4. **像素风主题三套** —— 纸色 / 夜幕 / 掌机 GB，一键切换并记住选择。
5. **中文字库按需精简** —— 从源码自动提取实际用到的 3158 个字符，生成 5 档 LVGL 字库（含 IPA 音标），仅 2.08MB。
6. **联网即对时** —— WiFi 连接后自动 NTP/HTTP 校时（东八区），主页时钟/日期即真。
7. **AI 挑战 + 板端 Web 管理** —— 板子自带 HTTP 管理服务（:8080），电脑浏览器直连即可配置小米 MiMo 大模型、查看学习数据；板端「AI 挑战」用大模型根据**你已学的单词**出英文例句挖空四选一题，答题后给出 AI 中文讲解。

---

## 二、选题方向

**AI 硬件产品创新**

理由：作品是一个软硬一体的独立学习设备形态（独占系统、开机即用、按键式交互），而非通用系统上的一个 App；系统层深度使用 openvela 的 LVGL 图形栈、NuttX 文件系统（YAFFS 持久化）、网络协议栈（wapi/NTP/HTTP）与板级驱动。

---

## 三、目录结构

```text
app/hello_app/                 ← 喵喵机应用（映射到 packages/demos/contest2026_193_hello_app）
├── mmj_main.c                 ← 主程序：五个页面 + 学习/闯关/复习调度 + 本地存储 + WiFi/对时
├── mmj_words.c / .h           ← CET-4 词库 7508 词（由脚本生成，勿手改）
├── Makefile / Kconfig / ...   ← 构建配置
└── README.md                  ← 应用说明

board/contest_board/           ← 板级适配（映射到 vendor/openvela/boards/contest2026_193_board）

docs/
└── 喵喵机方案.md               ← 设计方案与实现说明（页面/调度/存储/字库/构建）

tools/
├── mmj_admin/index.html       ← 词库管理页
├── gen_words.py               ← 词库生成器（CET-4 清洗 → mmj_words.c）
├── gen_board_fonts.js         ← 字库生成器（自动提字 → 5 档 LVGL bin 字体）
└── build_eye.sh               ← 一键构建+打包脚本（含失败拦截护栏）

.claude/skills/                ← AI 开发 Skill（AI Coding 沉淀）
├── lvgl-board-fonts/          ← 板端 LVGL 中文字库生成与应用工作流
└── openvela-image-verify/     ← 固件镜像防"假成功"验证与真机问题定位

logs/                          ← AI Coding 对话日志

logs/                          ← AI Coding 对话日志
```

---

## 四、运行方式

### 4.1 环境准备

```bash
# 拉取 openvela 全量源码 + 本仓（工作区根目录为本仓的上一级）
repo init -u https://github.com/open-vela/contest2026_193_FirstNext \
  -b dev-ai-contest-2026 -m contest2026_193_FirstNext.xml
repo sync -c -j8
```

- 编译环境：**WSL / Linux**（交叉工具链在 `prebuilts/` 内，无需另装）
- 应用配置：`menuconfig` 中启用 `Contest 2026 Team 193 MiaoMiaoJi App`（`CONFIG_LVX_USE_DEMO_CONTEST2026_193_EYE=y`）

### 4.2 编译

```bash
# 在 openvela 工作区根目录（本仓的上一级）
./build.sh vendor/allwinnertech/boards/r528/r528s3-dshanpi/configs/nsh/ -j8
```

或使用带护栏的打包脚本（编译失败即中止打包，避免"假成功"镜像）：

```bash
bash contest2026_193_FirstNext/tools/build_eye.sh
```

产物：

```
vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
```

> ⚠️ 注意：openvela 的 `build.sh` 在应用编译失败时**仍会继续打包**，会产出"构建成功但代码没进固件"的假镜像。请务必确认日志里应用被真正编译（出现 `.o`），或在镜像里检索新增的中文字符串。

### 4.3 烧录

用全志烧录工具（PhoenixSuit / LiveSuit）烧写上一步生成的 `.img` 到 SPI NAND，串口 115200 观察日志。

### 4.4 运行

烧录后板子自动启动应用：

- 首页 → 点「学习」背单词 → 点「闯关」做测验 → 点「词库」查状态 → 点「设置」换主题/连 WiFi
- 学习记录写在 `/data/mmj/words.json`，断电不丢

### 4.5 AI 挑战与 Web 管理

1. 板子连上 WiFi 后，在「设置 → 管理地址」查看板子 IP（如 `192.168.1.23:8080`）
2. 电脑接入**同一 WiFi**，浏览器打开该地址，填入小米 MiMo API Key 并保存
3. 板子「闯关 → AI 挑战」：大模型根据你已学的单词出 5 道例句挖空四选一题，答完逐题给出 AI 中文讲解

