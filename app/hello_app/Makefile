############################################################################
# Contest 2026 team 193 - 喵喵机 (MiaoMiaoJi) English Learning Machine
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = eye_app
PRIORITY  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_193_EYE_PRIORITY)
STACKSIZE = $(CONFIG_LVX_USE_DEMO_CONTEST2026_193_EYE_STACKSIZE)
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_193_EYE)

MAINSRC   = mmj_main.c

# 词库数据(7508 词, 由 tools/gen_words.py 生成)
CSRCS     = mmj_words.c

include $(APPDIR)/Application.mk
