# Contest 2026 team 087 Focus AIoT application
if(CONFIG_CONTEST2026_087_FOCUS_AIOT)
  nuttx_add_application(
    NAME focus_aiot
    SRCS
      focus_aiot_main.c
      src/lcd.c
      src/lcd_icons.c
      src/ui_draw.c
      src/mimo.c
    INCLUDE_DIRECTORIES include src
    STACKSIZE 4096)
endif()
