# Contest 2026 team 087 hello app (FOCUS AIoT MVP skeleton)
if(CONFIG_LVX_USE_DEMO_CONTEST2026_087_HELLO_APP)
  nuttx_add_application(
    NAME hello_app
    SRCS hello_app_main.c
    SRCS core/mock.c
    STACKSIZE 8192)
endif()
