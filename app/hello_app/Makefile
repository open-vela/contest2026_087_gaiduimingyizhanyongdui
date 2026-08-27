############################################################################
# Contest 2026 team 087 hello app - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = hello_app
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_087_HELLO_APP)

MAINSRC = hello_app_main.c
CSRCS = core/mock.c core/state_machine.c core/rgb565_jpeg.c \
        behavior/behavior.c behavior/behavior_config.c \
        ui/lcd.c ui/lcd_icons.c ui/mimo.c ui/ui_cjk_font.c ui/ui_draw.c \
        perception/perception.c

ifeq ($(CONFIG_CONTEST2026_087_LCD_STUB),y)
CSRCS += hardware/lcd_stub.c
else
CSRCS += hardware/lcd_st7789.c
endif

ifeq ($(CONFIG_CONTEST2026_087_WIFI_STUB),y)
CSRCS += hardware/wifi_stub.c
else
CSRCS += hardware/wifi_esp32.c
endif

ifeq ($(CONFIG_CONTEST2026_087_BUTTON_STUB),y)
CSRCS += hardware/button_stub.c
else
CSRCS += hardware/buttons.c
endif

ifeq ($(CONFIG_CONTEST2026_087_CAMERA_STUB),y)
CSRCS += hardware/camera_stub.c
else
CSRCS += hardware/camera_ov2640.c
endif

ifeq ($(CONFIG_CONTEST2026_087_AUDIO_STUB),y)
CSRCS += hardware/audio_stub.c
else
CSRCS += hardware/audio_i2s.c
endif

# 真实硬件驱动 (buttons.c/audio_i2s.c) 依赖 arch 私有 GPIO 头
# esp32s3_gpio.h / hardware/esp32s3_gpio_sigmap.h (esp32s3 板特定)
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/arch/xtensa/src/esp32s3

# perception mock 开关 (默认 mock, 关闭则走真实 MiMo 需 cJSON)
ifeq ($(CONFIG_CONTEST2026_087_PERCEPTION_MOCK),y)
CFLAGS += -DPERCEPTION_MOCK
endif

include $(APPDIR)/Application.mk
