/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Instantiate esp8266 devices for the Arduino STAR board */

#include <eagle_soc.h>
#include <bathos/init.h>
#include <bathos/event.h>
#include <bathos/esp8266-uart.h>
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

#ifdef CONFIG_TASK_LININOIO
/*
 * LininoIO raw channel
 */
static const struct lininoio_channel_descr cdescr = {
	/* Console core 0 (esp8266) */
	.contents_id = LININOIO_PROTO_ASCII_CONSOLE,
};

static const struct lininoio_channel_ops cops = {
	.input = lininoio_dev_input,
};

static struct lininoio_channel_data cdata;

declare_lininoio_channel(lininoio0, &cdescr, &cops, &cdata);
#endif /* CONFIG_TASK_LININOIO */
