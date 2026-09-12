#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
喵喵机 AI 出题服务（本地运行）

板子通过局域网 HTTP 访问本服务，本服务代理调用小米 MiMo 大模型生成
四选一闯关题（英文例句挖空），板端「AI 挑战」页面展示并判分。

运行:
    pip install flask requests
    python3 app.py            # 默认 0.0.0.0:8000

配置:
    浏览器打开 http://<本机局域网IP>:8000 填写 MiMo API Key（保存于本目录 config.json）
    板子「设置 → AI 服务」填入同一 <本机局域网IP>:8000
"""
import json
import os
import re

import requests
from flask import Flask, jsonify, request, send_from_directory

BASE = os.path.dirname(os.path.abspath(__file__))
CFG_PATH = os.path.join(BASE, "config.json")

DEFAULT_CFG = {
    "api_key": "",
    "base_url": "https://api.xiaomimimo.com/v1",
    "model": "mimo-v2.5-pro",
}

PROMPT = """你是英语学习应用「喵喵机」的出题助手。根据下面的 CET-4 单词列表，出 {count} 道四选一选择题。

要求:
1. 题干为英文例句挖空，空格用 ___ 表示，考查所给单词在句中的正确使用
2. 4 个选项中 1 个正确、3 个干扰项取自其余给定单词或该词的形近变形
3. explain 用一句简短中文讲解（考点/词义/搭配），不超过 30 字
4. 只输出 JSON 数组，不要输出任何其他文字或代码块标记，格式:
[{{"q":"例句","options":["A","B","C","D"],"ans":0,"word":"正确单词","cn":"中文释义","explain":"一句话讲解"}}]

单词列表:
{words}"""

app = Flask(__name__, static_folder="static", static_url_path="")


def load_cfg():
    try:
        with open(CFG_PATH, encoding="utf-8") as f:
            cfg = json.load(f)
    except Exception:
        cfg = {}
    out = dict(DEFAULT_CFG)
    out.update({k: v for k, v in cfg.items() if v})
    return out


def save_cfg(cfg):
    with open(CFG_PATH, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


@app.route("/")
def index():
    return send_from_directory("static", "index.html")


@app.route("/api/config", methods=["GET", "POST"])
def api_config():
    if request.method == "GET":
        cfg = load_cfg()
        cfg["api_key"] = mask_key(cfg["api_key"])
        return jsonify(cfg)
    cfg = request.get_json(force=True)
    cur = load_cfg()
    for k in ("api_key", "base_url", "model"):
        v = (cfg.get(k) or "").strip()
        if v and not is_masked(v):
            cur[k] = v
    save_cfg(cur)
    return jsonify({"code": 0})


@app.route("/api/config/test", methods=["POST"])
def api_test():
    cfg = load_cfg()
    if not cfg["api_key"]:
        return jsonify({"code": 2, "msg": "请先保存 API Key"})
    try:
        r = chat(cfg, [{"role": "user", "content": "回复 OK 两个字母即可"}], max_tokens=8)
        text = r.json()["choices"][0]["message"]["content"].strip()
        return jsonify({"code": 0, "msg": "连接成功: " + text[:40]})
    except Exception as e:
        return jsonify({"code": 1, "msg": "失败: %s" % e}), 200


def is_masked(s):
    return "*" in s


def mask_key(k):
    return (k[:6] + "******" + k[-4:]) if len(k) > 12 else k


def chat(cfg, messages, max_tokens=2048, timeout=60):
    return requests.post(
        cfg["base_url"].rstrip("/") + "/chat/completions",
        headers={"Authorization": "Bearer " + cfg["api_key"]},
        json={"model": cfg["model"], "messages": messages,
              "temperature": 0.7, "max_tokens": max_tokens},
        timeout=timeout,
    )


@app.route("/api/quiz", methods=["POST"])
def api_quiz():
    data = request.get_json(force=True) or {}
    words = data.get("words") or []
    count = max(1, min(int(data.get("count", 5)), 8))
    if not words:
        return jsonify({"code": 1, "msg": "no words"})

    cfg = load_cfg()
    if not cfg["api_key"]:
        return jsonify({"code": 2, "msg": "server: api key not set"})

    wl = "\n".join("- {w} ({pos}) {cn}".format(
        w=w.get("w", ""), pos=w.get("pos", ""), cn=w.get("cn", "")) for w in words)

    try:
        r = chat(cfg, [{"role": "user", "content": PROMPT.format(count=count, words=wl)}])
        r.raise_for_status()
        content = r.json()["choices"][0]["message"]["content"]
    except Exception as e:
        return jsonify({"code": 3, "msg": "LLM error: %s" % e})

    m = re.search(r"\[[\s\S]*\]", content)
    if not m:
        return jsonify({"code": 4, "msg": "LLM 返回不含 JSON 数组", "raw": content[:200]})
    try:
        quiz = json.loads(m.group(0))
    except Exception as e:
        return jsonify({"code": 5, "msg": "JSON parse: %s" % e, "raw": m.group(0)[:200]})

    quiz = [q for q in quiz if q.get("q") and q.get("options") and len(q["options"]) == 4]
    return jsonify({"code": 0, "quiz": quiz[:count]})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000)
