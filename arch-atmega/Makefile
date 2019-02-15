# The first item is for CFLAGS, the second for avrdude
ifeq ($(CONFIG_MACH),"atmega8")
BATHOS_MCU = atmega8 m8
else ifeq ($(CONFIG_MACH),"atmega32u4")
BATHOS_MCU = atmega32u4 m32u4
else ifeq ($(CONFIG_MACH), "atmega328p")
BATHOS_MCU = atmega328p m5
else
BATHOS_MCU = atmega8 m8
endif

ifeq ($(CONFIG_ADD_EXTERNAL_BOOTLOADER),y)
BOOTLOADER_HEX=$(shell echo $(CONFIG_EXTERNAL_BOOTLOADER_PATH))
BOOTLOADER=$(ADIR)/bootloader.o
else
BOOTLOADER_HEX=
BOOTLOADER=
endif

ARCH_EXTRA := $(ADIR)/bootloader.lds $(ADIR)/bootloader.o


# Similary, the user can set TTY= when running "make load"
TTY ?= /dev/ttyUSB0

# By default, we only have the uart tasks
TASK-y ?= task-uart.o

# Following stuff is calculated, no further customization expected

MCU=$(word 1, $(BATHOS_MCU))
CFLAGS  = -mmcu=$(MCU) -DMCU_$(MCU)=1 -funsigned-char -funsigned-bitfields \
-ffunction-sections -fpack-struct -fshort-enums -DARCH_HAS_BUILTIN_FFS
LDFLAGS = -nostdlib -Wl,-Map=bathos.map -mmcu=$(MCU)
LDS = $(ADIR)/bathos.lds
ASFLAGS = $(CFLAGS)
ASFLAGS += -mmcu=$(word 1, $(BATHOS_MCU)) -I$(SRC_DIR)/include \
-I$(SRC_DIR)/$(ADIR)
ARCH_EXTRA_LIBS = -lc

# Library: we have one assembly source file
LIBARCH = $(ADIR)/libarch.a

ifeq ($(MCU),atmega8)
LIBARCH_OBJS += $(ADIR)/atmega-a8.o
else ifeq ($(MCU),atmega32u4)
LIBARCH_OBJS += $(ADIR)/atmega-32u4.o
else ifeq ($(MCU),atmega328p)
LIBARCH_OBJS += $(ADIR)/atmega328p.o
endif

$(ADIR)/libarch.a: $(LIBARCH_OBJS)
	$(AR) r $@ $(LIBARCH_OBJS)

ifeq ($(MCU),atmega32u4)
AOBJ = $(ADIR)/main.o $(ADIR)/idle.o $(ADIR)/gpio.o \
	$(ADIR)/adc.o $(ADIR)/pwm.o
endif

ifeq ($(MCU),atmega328p)
AOBJ = $(ADIR)/main.o $(ADIR)/idle.o $(ADIR)/gpio.o
endif

ifeq ($(CONFIG_BOARD),yun)
AOBJ += $(ADIR)/uart-mux-yun.o
endif

ifeq ($(CONFIG_ATMEGA_USB_UART),y)
AOBJ += $(ADIR)/usb-uart.o
endif

ifeq ($(CONFIG_ATMEGA_UART),y)
AOBJ += $(ADIR)/uart.o
endif

ifeq ($(CONFIG_ATMEGA_SPI),y)
AOBJ += $(ADIR)/spi.o
endif

VECTOR_OBJS := $(foreach i,$(shell seq 1 $$(($(NR_INTERRUPTS) - 1))),\
		  $(ADIR)/vector_$i.o)
DEFAULT_ISR_OBJS := $(foreach i,$(shell seq 1 $$(($(NR_INTERRUPTS) - 1))),\
		  $(ADIR)/default_isr_$i.o)

AOBJ += $(DEFAULT_ISR_OBJS)

$(VECTOR_OBJS): $(ADIR)/vector_%.o : $(ADIR)/vector.c
	$(CC) $(CFLAGS) -DVECNO=$* -c -o $@ $<

$(DEFAULT_ISR_OBJS): $(ADIR)/default_isr_%.o : $(ADIR)/default_isr.c
	$(CC) $(CFLAGS) -DVECNO=$* -c -o $@ $<

$(ADIR)/vector_0.o: $(ADIR)/vector_0.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(ADIR)/boot.o: $(ADIR)/reset.o $(VECTOR_OBJS) $(ADIR)/vector_0.o
	$(LD) -T $(ADIR)/boot.lds -r $(ADIR)/vector_0.o $(VECTOR_OBJS) \
	$(ADIR)/reset.o -o $@

# and the load target, not there in other architectures
load: bathos.hex
	avrdude -p $(word 2, $(BATHOS_MCU)) \
		-c stk500v2 -P $(TTY) -e -U flash:w:bathos.hex

ifneq ($(BOOTLOADER_HEX),)

$(ADIR)/bootloader.bin: $(BOOTLOADER_HEX)
	$(OBJCOPY) --gap-fill 0xff -I ihex -O binary $< $@

$(ADIR)/bootloader.o: $(ADIR)/bootloader.bin
	$(OBJCOPY) -I binary -O elf32-avr --rename-section \
	    .sec1=.bootloader,contents,alloc,load,readonly,code $< $@

$(ADIR)/bootloader.lds:
	echo \
	".bootloader 0x7000 : AT(0x7000) { arch-atmega/bootloader.o(*); }" > $@

else
$(ADIR)/bootloader.o:
	rm -f $@ && touch $@

$(ADIR)/bootloader.lds:
	echo "" > $@
endif

clean: cleanhex

cleanhex:
	rm -f bathos.hex

.PHONY: $(ADIR)/bootloader.lds $(ADIR)/bootloader.o $(ADIR)/bootloader.bin
