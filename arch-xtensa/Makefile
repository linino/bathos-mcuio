
ARCH_CFLAGS = -Os -g -O2 -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals -D__ets__

ARCH_LDFLAGS = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

MACH_DIR = $(ADIR)/mach-$(shell echo $(CONFIG_MACH))

ARCH_CFLAGS += -I$(SRC_DIR)/$(MACH_DIR) -DARCH_HAS_BUILTIN_FFS
ARCH_LDFLAGS += -L$(SRC_DIR)/$(MACH_DIR)

ifneq ($(ESP8266_STATION_HOSTNAME),)
ARCH_CFLAGS += -DESP8266_STATION_HOSTNAME=\"$(ESP8266_STATION_HOSTNAME)\"
endif

ifneq ($(ESP8266_ESSID),)
ARCH_CFLAGS += -DESP8266_ESSID=\"$(ESP8266_ESSID)\"
endif

ifneq ($(ESP8266_PASSWD),)
ARCH_CFLAGS += -DESP8266_PASSWD=\"$(ESP8266_PASSWD)\"
endif

# No boot code for this machine at the moment, we just build libraries.
BOOT_OBJ = $(ADIR)/init.o

# No io.o for this arch
IO_OBJ =

# Library
LIBARCH = $(BUILD_DIR)/$(ADIR)/libarch.a
LIBARCH_OBJS = $(ADIR)/jiffies.o

ARCH_EXTRA_LIBS = -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain

ifeq ($(CONFIG_MACH_EAGLE),y)
AOBJ += $(MACH_DIR)/esp8266-wifi.o
AOBJ += $(MACH_DIR)/gpio.o
AOBJ += $(MACH_DIR)/irq.o
endif

ifeq ($(CONFIG_BOARD_STAR_8266),y)
LIBARCH_OBJS += $(MACH_DIR)/board-star.o
endif

ifeq ($(CONFIG_BOARD_MODULINO_8266),y)
LIBARCH_OBJS += $(MACH_DIR)/board-modulino.o
endif

ifeq ($(CONFIG_BOARD_PRIMO_MINI_8266),y)
LIBARCH_OBJS += $(MACH_DIR)/board-primo-mini.o
endif

LDS = $(wildcard $(SRC_DIR)/$(MACH_DIR)/bathos$(MODE).lds)

CFLAGS = $(ARCH_CFLAGS)
LDFLAGS = $(ARCH_LDFLAGS)

$(LIBARCH): $(LIBARCH_OBJS)
	$(AR) r $@ $(LIBARCH_OBJS)
