/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

/*
 * Uart driver for bathos
 */

#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/circ_buf.h>
#include <bathos/string.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/shell.h>
#include <bathos/stdlib.h>
#include <bathos/allocator.h>
#include <bathos/dev_ops.h>
#include <arch/hw.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#define UCSRB(n)	xcat(xcat(UCSR,n),B)
# define RXEN(n)	xcat(RXEN,n)
# define RXCIE(n)	xcat(RXCIE,n)
# define TXEN(n)	xcat(TXEN,n)
# define RXEN(n)	xcat(RXEN,n)

#define UCSRA(n) xcat(xcat(UCSR,n),A)
# define UDRE(n) xcat(UDRE,n)

#define UDR(n) xcat(UDR,n)
#define UBRR(n) xcat(UBRR,n)

static uint8_t initialized;
static struct bathos_dev __uart_dev;

#ifdef CONFIG_MACH_ATMEGA328P
#define UART_NO 0
#elif defined CONFIG_MACH_ATMEGA32U4
#define UART_NO 1
#else
#error "DEFINE UART_NO BEFORE COMPILING THIS FILE"
#endif

#if defined CONFIG_MACH_ATMEGA32U4
# define ISR_NAME USART1_RX_vect
#elif defined CONFIG_MACH_ATMEGA328P
# define ISR_NAME USART_RX_vect
#else
# define ISR_NAME xcat(xcat(USART, UART_NO),_RX_vect)
#endif

static int atmega_uart_rx_enable(void *priv)
{
	UCSRB(UART_NO) |= (1 << RXEN(UART_NO)) | (1 << RXCIE(UART_NO));
	return 0;
}

static int atmega_uart_tx_enable(void *priv)
{
	UCSRB(UART_NO) |= (1 << TXEN(UART_NO));
	return 0;
}

static int atmega_uart_rx_disable(void *priv)
{
	UCSRB(UART_NO) &= ~((1 << RXEN(UART_NO)) | (1 << RXCIE(UART_NO)));
	return 0;
}

static int atmega_uart_tx_disable(void *priv)
{
	UCSRB(UART_NO) &= ~(1 << TXEN(UART_NO));
	return 0;
}

static int atmega_uart_putc(void *priv, const char c)
{
	while (!(UCSRA(UART_NO) & (1 << UDRE(UART_NO))));
	UDR(UART_NO) = c;
	return 1;
}

static const struct bathos_ll_dev_ops PROGMEM atmega_uart_ops = {
	.putc = atmega_uart_putc,
	.rx_disable = atmega_uart_rx_disable,
	.rx_enable = atmega_uart_rx_enable,
	.tx_disable = atmega_uart_tx_disable,
	.tx_enable = atmega_uart_tx_enable,
};

static int atmega_uart_set_baudrate(uint32_t baud)
{
	UBRR(UART_NO) = (THOS_QUARTZ / 16) / baud - 1;
	return 0;
}

static int atmega_uart_init(void)
{
	void *udata;
	uint32_t baud = 250000; /* Target baud rate */
	if (initialized)
		return 0;
	atmega_uart_set_baudrate(baud);
	udata = bathos_dev_init(&__uart_dev, &atmega_uart_ops, NULL);
	if (!udata)
		return -ENODEV;
	__uart_dev.priv = udata;
	initialized = 1;
	return 0;
}

#if defined CONFIG_CONSOLE_UART && CONFIG_EARLY_CONSOLE
int console_early_init(void)
{
	return atmega_uart_init();
}
#else
rom_initcall(atmega_uart_init);
#endif


ISR(ISR_NAME, __attribute__((section(".text.ISR"))))
{
	char c = UDR(UART_NO);

	(void)bathos_dev_push_chars(&__uart_dev, &c, 1);
}

static int atmega_uart_open(struct bathos_pipe *pipe)
{
	int stat = 0;

	if (!initialized)
		stat = atmega_uart_init();
	if (stat)
		return stat;
	return bathos_dev_open(pipe);
}

const struct bathos_dev_ops PROGMEM uart_dev_ops = {
	.open = atmega_uart_open,
	.read = bathos_dev_read,
	.write = bathos_dev_write,
	.close = bathos_dev_close,
	.ioctl = bathos_dev_ioctl,
};

static struct bathos_dev __uart_dev __attribute__((section(".bathos_devices"),
					    aligned(2))) = {
	.name = "avr-uart",
	.ops = &uart_dev_ops,
};

/* baudrate command */
#define NBAUDRATES 2

struct baudrate {
	uint32_t val;
	char *descr; /* needed because printf of val fails for val > 32767 */
};

static const struct baudrate PROGMEM baud[NBAUDRATES] = {
	{125000, "125000"},
	{250000, "250000"}
};

static uint8_t baud_idx = 1;

static int baudrate_handler(int argc, char *argv[])
{
	int ret = 0, i;

	if (argc > 1) {
		i = argv[1][0] - '1';
		if ((i < 0) || (i > NBAUDRATES)) {
			printf("Invalid baudrate: %d\n", i);
			ret = -EINVAL;
		}
		else {
			baud_idx = i;
			atmega_uart_set_baudrate(baud[i].val);
		}
	}

	for (i = 0; i < NBAUDRATES; i++) {
		if (i == baud_idx)
			printf("*");
		else
			printf(" ");
		printf(" %d: %s\n", i + 1, baud[i].descr);
	}

	return ret;
}

static void baudrate_help(int argc, char *argv[])
{
	printf("show/set uart baudrate\n");
}

declare_shell_cmd(baudrate, baudrate_handler, baudrate_help);
