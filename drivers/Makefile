
# Drivers Makefile: just add the objects and names

LIBDRIVERS = $(BUILD_DIR)/drivers/libdrivers.a

ifeq ($(CONFIG_HAVE_SPI),y)
DRV_OBJS += drivers/enc28j60.o
endif

DRV_OBJS += drivers/w1.o
ifeq ($(CONFIG_HAVE_GPIOS),y)
DRV_OBJS += drivers/w1-gpio.o
endif
DRV_OBJS += drivers/w1-temp.o
DRV_OBJS += drivers/w1-eeprom.o
DRV_OBJS += drivers/dev.o

ifeq ($(CONFIG_HAVE_USB_GENERIC_STACK),y)
DRV_OBJS += drivers/usb-cdc-acm.o
DRV_OBJS += drivers/usb-device-controller.o drivers/usb-device-ep.o
DRV_OBJS += drivers/usb-descriptors.o drivers/usb-data.o
endif

ifeq ($(CONFIG_DEBUG_BITBANG),y)
DRV_OBJS += drivers/debug_bitbang.o
endif

ifeq ($(CONFIG_FREESCALE_DRIVERS),y)
  DRV_OBJS += drivers/freescale-uart.o
  DRV_OBJS += drivers/usb-device-freescale.o
endif

ifeq ($(CONFIG_AMBA_DRIVERS),y)
  DRV_OBJS += drivers/pl011-uart.o
endif

ifeq ($(CONFIG_NRF5X_DRIVERS),y)
  DRV_OBJS += drivers/nrf5x-rtc.o
  DRV_OBJS += drivers/nrf5x-uart.o
  DRV_OBJS += drivers/nrf5x-radio.o
endif

ifeq ($(CONFIG_STM32F4X_DRIVERS),y)
  DRV_OBJS += drivers/stm32f4x-uart.o
endif

ifeq ($(CONFIG_ESP8266_DRIVERS), y)
  DRV_OBJS += drivers/esp8266-uart.o
  DRV_OBJS += drivers/esp8266-spim.o
  DRV_OBJS += drivers/esp8266-wlan.o
endif

ifeq ($(CONFIG_LININOIO_DRIVERS), y)
  DRV_OBJS += drivers/lininoio-dev.o
endif

ifeq ($(CONFIG_UNIX_ETH_DRIVER), y)
  DRV_OBJS += drivers/unix-eth.o
endif

ifeq ($(CONFIG_BP_SPIM_DRIVER), y)
  DRV_OBJS += drivers/bp-spim.o
endif

DRV_OBJS += drivers/radio-bridge-master.o
DRV_OBJS += drivers/radio-bridge-slave.o

$(LIBDRIVERS): $(DRV_OBJS)
	$(AR) r $@ $(DRV_OBJS)

LIBS += $(LIBDRIVERS)
LDFLAGSS += -Ldrivers -ldrivers

drivers/usb-descriptors.o: drivers/usb-descriptors.c

drivers/usb-descriptors.c: $(patsubst "%",%,$(CONFIG_USB_CONFIG_FILE))
	scripts/gen_usb_descr $< $@

drivers/usb-data.o: drivers/usb-data.c


drivers/usb-data.c: $(patsubst "%",%,$(CONFIG_USB_CONFIG_FILE))
	CONFIG_MACH=$(CONFIG_MACH) scripts/gen_usb_data $< $@
