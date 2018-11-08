
ARCH_CFLAGS = -Os -g -O2 -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals -D__ets__

ARCH_LDFLAGS = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

MACH_DIR = $(ADIR)/mach-$(shell echo $(CONFIG_MACH))

ARCH_CFLAGS += -I$(SRC_DIR)/$(MACH_DIR) -DARCH_HAS_BUILTIN_FFS

# No boot code for this machine at the moment, we just build libraries.
BOOT_OBJ =

# No io.o for this arch
IO_OBJ =

# Library
LIBARCH = $(BUILD_DIR)/$(ADIR)/libarch.a
LIBARCH_OBJS = $(ADIR)/init.o $(ADIR)/jiffies.o

ARCH_EXTRA_LIBS = -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain

ifeq ($(CONFIG_MACH_EAGLE),y)
LIBARCH_OBJS += $(MACH_DIR)/irq.o
endif

ifeq ($(CONFIG_BOARD_STAR_8266),y)
LIBARCH_OBJS += $(MACH_DIR)/board-star.o
endif

LDS = $(wildcard $(SRC_DIR)/$(MACH_DIR)/bathos$(MODE).lds)

CFLAGS = $(ARCH_CFLAGS)
LDFLAGS = $(ARCH_LDFLAGS)

$(LIBARCH): $(LIBARCH_OBJS)
	$(AR) r $@ $(LIBARCH_OBJS)
