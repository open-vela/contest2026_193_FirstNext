/****************************************************************************
 * mmj_words.h - 喵喵机词库(CET-4, 7508 词)
 *
 * 词库数据基于开源数据集 english-vocabulary
 * (MIT License, Copyright (c) KyleBing) 提供的 CET-4 词表,
 * 经 tools/gen_words.py 清洗(首条释义/首条例句/英式+美式音标)。
 * 本文件由脚本生成, 请勿手改; 更新词库请重跑生成脚本。
 ****************************************************************************/

#ifndef __MMJ_WORDS_H
#define __MMJ_WORDS_H

#define MMJ_NWORD 7508          /* 编译期常量, 用于数组维度 */

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
