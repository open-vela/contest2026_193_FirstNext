#!/usr/bin/env python3
"""
喵喵机词库生成器 —— 生成 app/hello_app/mmj_words.{h,c}

词库数据: 基于开源数据集 english-vocabulary
          (MIT License, Copyright (c) KyleBing) 的 CET-4 词表
          full_line_jsonl/full/正序/四级.jsonl (7508 词)
清洗规则: 取首条释义(词性+中文) / 首条例句(英+中) / 英式+美式音标

用法:
    # 1) 下载原始词表(约 18.5MB) 到本地
    curl -sL -o /tmp/cet4_full.jsonl \
      "https://raw.githubusercontent.com/KyleBing/english-vocabulary/master/full_line_jsonl/full/%E6%AD%A3%E5%BA%8F/%E5%9B%9B%E7%BA%A7.jsonl"

    # 2) 生成(可传词数上限, 省略=全部)
    python3 tools/gen_words.py            # 全部 7508 词
    python3 tools/gen_words.py 500        # 仅前 500 词

生成后需重跑 tools/gen_board_fonts.js 更新字库(新增汉字的字形)。
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT_DIR = os.path.join(REPO, "app", "hello_app")
SRC = os.environ.get("CET4_JSONL", "/tmp/cet4_full.jsonl")
LIMIT = int(sys.argv[1]) if len(sys.argv) > 1 else 0


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def ipa(p):
    if not p:
        return ""
    return "/" + p.strip().strip("/").replace("'", "ˈ") + "/"


def main():
    if not os.path.exists(SRC):
        print("找不到词表:", SRC)
        print("见本文件头部注释的下载命令")
        return 1

    words = []
    with open(SRC, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            head = (o.get("headWord") or "").strip()
            if not head:
                continue
            c = o.get("content", {}).get("word", {}).get("content", {})
            tr = (c.get("trans") or [{}])[0]
            pos = (tr.get("pos") or "").strip()
            cn = (tr.get("tranCn") or "").strip()
            if not cn:
                continue
            if pos and not pos.endswith("."):
                pos += "."
            ex = exz = ""
            ss = c.get("sentence", {}).get("sentences") or []
            if ss:
                ex = (ss[0].get("sContent") or "").strip()
                exz = (ss[0].get("sCn") or "").strip()
            words.append({
                "w": head, "uk": ipa(c.get("ukphone")), "us": ipa(c.get("usphone")),
                "pos": pos, "cn": cn, "ex": ex, "exz": exz})
            if LIMIT and len(words) >= LIMIT:
                break

    n = len(words)
    print("提取词数:", n)
    if n == 0:
        return 1

    hdr = '''/****************************************************************************
 * mmj_words.h - 喵喵机词库(CET-4, %d 词)
 *
 * 数据来源: KyleBing/english-vocabulary (MIT) 的 CET-4 正序词表,
 *          经 tools/gen_words.py 清洗(首条释义/首条例句/英式+美式音标)。
 * 本文件由脚本生成, 请勿手改; 更新词库请重跑生成脚本。
 ****************************************************************************/

#ifndef __MMJ_WORDS_H
#define __MMJ_WORDS_H

#define MMJ_NWORD %d          /* 编译期常量, 用于数组维度 */

typedef struct
{
  const char *w;    /* 单词 */
  const char *uk;   /* 英式音标(IPA) */
  const char *us;   /* 美式音标(IPA) */
  const char *pos;  /* 词性 */
  const char *cn;   /* 中文释义 */
  const char *ex;   /* 例句(英) */
  const char *exz;  /* 例句(中) */
} mmj_word_t;

extern const mmj_word_t MMJ_WORDS[MMJ_NWORD];

#endif
''' % (n, n)
    with open(os.path.join(OUT_DIR, "mmj_words.h"), "w", encoding="utf-8") as f:
        f.write(hdr)

    cpath = os.path.join(OUT_DIR, "mmj_words.c")
    with open(cpath, "w", encoding="utf-8") as f:
        f.write('/* 自动生成, 勿手改. 见 mmj_words.h */\n#include "mmj_words.h"\n\n')
        f.write("const mmj_word_t MMJ_WORDS[MMJ_NWORD] =\n{\n")
        for x in words:
            f.write('  {"%s", "%s", "%s", "%s",\n   "%s",\n   "%s", "%s"},\n' % (
                esc(x["w"]), esc(x["uk"]), esc(x["us"]), esc(x["pos"]),
                esc(x["cn"]), esc(x["ex"]), esc(x["exz"])))
        f.write("};\n")

    print("生成:", os.path.join(OUT_DIR, "mmj_words.h"))
    print("生成:", cpath, os.path.getsize(cpath), "bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
