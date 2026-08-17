# Contest 2026 team 087 hello app (FOCUS AIoT MVP skeleton)
if(CONFIG_LVX_USE_DEMO_CONTEST2026_087_HELLO_APP)
  set(FOCUS_AIOT_SRCS
      hello_app_main.c
      core/mock.c
      core/state_machine.c
      behavior/behavior.c
      behavior/behavior_config.c
      ui/lcd.c
      ui/lcd_icons.c
      ui/mimo.c
      ui/ui_cjk_font.c
      ui/ui_draw.c)
  if(CONFIG_CONTEST2026_087_LCD_STUB)
    list(APPEND FOCUS_AIOT_SRCS hardware/lcd_stub.c)
  else()
    list(APPEND FOCUS_AIOT_SRCS hardware/lcd_st7789.c)
  endif()
  if(CONFIG_CONTEST2026_087_WIFI_STUB)
    list(APPEND FOCUS_AIOT_SRCS hardware/wifi_stub.c)
  endif()
  nuttx_add_application(
    NAME hello_app
    SRCS ${FOCUS_AIOT_SRCS}
    STACKSIZE 8192)
endif()
