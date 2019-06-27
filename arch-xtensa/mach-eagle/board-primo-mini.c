/*
 * Copyright (c) dog hunter ABG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Instantiate esp8266 devices for the PRIMO board */
#define DEBUG 1
#include <mach/hw.h>
#include <eagle_soc.h>
#include <bathos/io.h>
#include <bathos/init.h>
#include <bathos/event.h>
#include <bathos/esp8266-uart.h>
#include <bathos/esp8266-spim.h>
#include <bathos/esp8266-wlan.h>
#include <bathos/dev_ops.h>
#include <bathos/pipe.h>
#ifdef CONFIG_TASK_LININOIO
#include <lininoio.h>
#include <bathos/lininoio-internal.h>
#include <bathos/lininoio-dev.h>
#endif

#define CONFIG_CONSOLE_UART0 1

static void uart0_pin_setup(const struct esp8266_uart_platform_data *plat)
{
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
}

const struct esp8266_uart_platform_data uart0_pdata = {
	.index = 0,
	.base = 0x60000000,
	.pin_setup = uart0_pin_setup,
};

#ifdef CONFIG_CONSOLE_UART0
int console_init(void)
{
	esp8266_uart_console_init(&uart0_pdata);
	console_putc('!');
	return 0;
}

void console_putc(int c)
{
	esp8266_uart_console_putc(&uart0_pdata, c);
}
#endif

const struct esp8266_wlan_platform_data wlan0_pdata = {
	.nbufs = 4,
	.bufsize = 64,
};

static struct bathos_dev __wdev0
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "wlan0",
	.ops = &esp8266_wlan_dev_ops,
	.platform_data = &wlan0_pdata,
};

static int _spim_pin_setup(int instance)
{
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, 2);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, 2);
	return 0;
}

static const struct esp8266_spim_platform_data spim0_pdata = {
	.instance = 0,
	.nbufs = 4,
	.bufsize = 64,
	.base = 0x60000100,
	.setup_pins = _spim_pin_setup,
};

static struct bathos_dev __spimdev0
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "spim0",
	.ops = &esp8266_spim_dev_ops,
	.platform_data = &spim0_pdata,
};

void bathos_int_handler_name(SPI0_IRQN)(struct event_handler_data *data)
{
	uint32_t v;
	v = readl(0x60000000 + 0x230);
	v &= ~0x3ff;
	writel(v, 0x60000000 + 0x230);
}

void bathos_int_handler_name(SPI1_IRQN)(struct event_handler_data *data)
{
	esp8266_spim_int_handler(&__spimdev0);
}
