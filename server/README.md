# 喵喵机 AI 出题服务

板子（AI 挑战页）通过局域网 HTTP 调用本服务；本服务代理调用**小米 MiMo 大模型**生成英文例句挖空四选一题与中文讲解。

```
板子(连 WiFi) ──HTTP──> 本服务(PC, 本地运行) ──HTTPS──> 小米 MiMo
  AI 挑战页               /api/quiz                      生成题目+讲解
```

## 运行（PC 上）

```bash
pip install -r requirements.txt
python3 app.py          # 监听 0.0.0.0:8000
```

首次运行：浏览器打开 `http://<本机局域网IP>:8000`，填入 MiMo API Key（在 platform.xiaomimimo.com 获取）、Base URL、模型名并保存（存于本目录 `config.json`）。

## 板端配置

板子与电脑接入**同一 WiFi**；板子「设置 → AI 服务」填入电脑的局域网 IP 与端口（如 `192.168.1.100:8000`）。本页地址栏里就是这个 IP。

## 接口

### POST /api/quiz

请求：

```json
{"count": 5, "words": [{"w":"abandon","pos":"v.","cn":"放弃；抛弃"}]}
```

响应：

```json
{"code":0, "quiz":[{"q":"He ___ his car in the snow.",
 "options":["abandoned","abandon","abandoning","abandons"],
 "ans":0,"word":"abandon","cn":"放弃；抛弃","explain":"时态与从句保持一致"}]}
```

`code` 非 0 时 `msg` 为错误说明（如 API Key 未配置、模型返回异常）。

### GET/POST /api/config

查看/保存 MiMo 配置；`POST /api/config/test` 测试连通性。
