############################################################################
# Contest 2026 team 087 Focus AIoT application
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = focus_aiot
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_CONTEST2026_087_FOCUS_AIOT)

MAINSRC = focus_aiot_main.c
CSRCS   = src/lcd.c src/lcd_icons.c src/ui_draw.c src/mimo.c
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/demos/contest2026_087_focus_aiot/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/demos/contest2026_087_focus_aiot/src

include $(APPDIR)/Application.mk
