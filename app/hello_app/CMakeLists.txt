# Contest 2026 team 193 - 喵喵机 (MiaoMiaoJi) English Learning Machine
if(CONFIG_LVX_USE_DEMO_CONTEST2026_193_EYE)
  nuttx_add_application(
    NAME eye_app
    SRCS mmj_main.c
    STACKSIZE 40960
    PRIORITY 100
    DEPENDS lvgl
  )
endif()
