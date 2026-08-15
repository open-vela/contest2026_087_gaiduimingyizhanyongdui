############################################################################
# Contest 2026 team 000 hello app - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = hello_app
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_087_HELLO_APP)

MAINSRC = hello_app_main.c
CSRCS   = core/mock.c core/state_machine.c \
          ui/lcd.c ui/lcd_icons.c ui/mimo.c ui/ui_cjk_font.c ui/ui_draw.c

ifeq ($(CONFIG_CONTEST2026_087_LCD_STUB),y)
CSRCS  += hardware/lcd_stub.c
else
CSRCS  += hardware/lcd_st7789.c
endif

ifeq ($(CONFIG_CONTEST2026_087_WIFI_STUB),y)
CSRCS  += hardware/wifi_stub.c
endif

include $(APPDIR)/Application.mk
