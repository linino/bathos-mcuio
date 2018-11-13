/*
 * Copyright (c) Davide Ciminaghi
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#include <arch/gpio.h>
#include <arch/bathos-arch.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/delay.h>
#include <bathos/stdio.h>
#include <bathos/circ_buf.h>
#include <bathos/string.h>
#include <bathos/io.h>
#include <bathos/irq.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/pl011-uart.h>
#include <bathos/bitops.h>
#include <bathos/gpio.h>

#define REG_UART_DR(b)		((b) / sizeof(uint32_t))
#define REG_UART_RSR(b)		((b) / sizeof(uint32_t))
#define REG_UART_ECR(b)		(((b) + 0x04) / sizeof(uint32_t))

#define REG_UART_FR(b)		(((b) + 0x18) / sizeof(uint32_t))
#define BUSY BIT(3)

#define REG_UART_IBRD(b)	(((b) + 0x24) / sizeof(uint32_t))
#define REG_UART_FBRD(b)	(((b) + 0x28) / sizeof(uint32_t))
#define REG_UART_LCR_H(b)	(((b) + 0x2c) / sizeof(uint32_t))

#define REG_UART_CR(b)		(((b) + 0x30) / sizeof(uint32_t))
#define RXE BIT(9)
#define TXE BIT(8)

#define REG_UART_IFLS(b)	(((b) + 0x34) / sizeof(uint32_t))

#define REG_UART_IMSC(b)	(((b) + 0x38) / sizeof(uint32_t))
#define RXIE BIT(4)

#define REG_UART_RIS(b)		(((b) + 0x3c) / sizeof(uint32_t))
#define REG_UART_MIS(b)		(((b) + 0x40) / sizeof(uint32_t))
#define RXI BIT(4)

#define REG_UART_ICR(b)		(((b) + 0x44) / sizeof(uint32_t))
#define REG_UART_DMACR(b)	(((b) + 0x48) / sizeof(uint32_t))

struct pl011_uart_priv {
	const struct pl011_uart_platform_data *platform_data;
	struct bathos_dev_data *dev_data;
};

static int pl011_uart_rx_enable(void *_priv)
{
	struct pl011_uart_priv *priv = (struct pl011_uart_priv*)_priv;
	const struct pl011_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	bathos_enable_irq(plat->irq);
	regs[REG_UART_CR(base)] |= RXE;
	regs[REG_UART_IMSC(base)] |= RXIE;
	return 0;
}

static int pl011_uart_tx_enable(void *_priv)
{
	struct pl011_uart_priv *priv = (struct pl011_uart_priv*)_priv;
	const struct pl011_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	regs[REG_UART_CR(base)] |= TXE;
	return 0;
}

static int pl011_uart_rx_disable(void *_priv)
{
	struct pl011_uart_priv *priv = (struct pl011_uart_priv*)_priv;
	const struct pl011_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	regs[REG_UART_CR(base)] |= RXE;
	regs[REG_UART_IMSC(base)] &= ~RXIE;
	bathos_disable_irq(plat->irq);
	return 0;
}

static int pl011_uart_tx_disable(void *_priv)
{
	struct pl011_uart_priv *priv = (struct pl011_uart_priv*)_priv;
	const struct pl011_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	regs[REG_UART_CR(base)] &= ~TXE;
	return 0;
}

void pl011_uart_console_putc(uint32_t base, const char c)
{
	while (regs[REG_UART_FR(base)] & BUSY)
		;
	regs[REG_UART_DR(base)] = c;
}

static int pl011_uart_putc(void *_priv, const char c)
{
	struct pl011_uart_priv *priv = (struct pl011_uart_priv*)_priv;
	const struct pl011_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	pl011_uart_console_putc(base, c);
	return 1;
}

const struct bathos_ll_dev_ops pl011_ll_uart_dev_ops = {
	.putc = pl011_uart_putc,
	.rx_disable = pl011_uart_rx_disable,
	.rx_enable = pl011_uart_rx_enable,
	.tx_disable = pl011_uart_tx_disable,
	.tx_enable = pl011_uart_tx_enable,
};

void pl011_uart_irq_handler(struct bathos_dev *dev)
{
	const struct pl011_uart_platform_data *plat = dev->platform_data;
	uint32_t base = plat->base;
	char c;
	uint32_t s1;

	s1 = regs[REG_UART_MIS(base)];
	if ((s1 & RXI) == 0)
		/* Spurious irq ? */
		return ;
	c = regs[REG_UART_DR(base)];
	(void)bathos_dev_push_chars(dev, &c, 1);
}

static int pl011_uart_set_baudrate(uint32_t base,
				       int clock,
				       uint32_t baud)
{
	/* set BDH and BDL */
	uint32_t bd = clock / 16 / baud;
	/* FIXME IMPLEMENT THIS */
	return 0;
}

static int
pl011_uart_hw_init(const struct pl011_uart_platform_data *plat,
		       int enable_rx, int enable_tx)
{
	int ret = 0;

	if (plat->power)
		ret = plat->power(plat, 1);
	if (ret < 0)
		return ret;
	ret = pl011_uart_set_baudrate(plat->base, plat->clock, 115200);
	if (ret < 0)
		return ret;
	if (enable_tx)
		regs[REG_UART_CR(plat->base)] |= TXE;
	if (enable_rx) {
		regs[REG_UART_CR(plat->base)] |= RXE;
		regs[REG_UART_IMSC(plat->base)] |= RXIE;
	}
	return ret;
}

int pl011_uart_console_init(const struct pl011_uart_platform_data *plat)
{
	int ret;

	ret = pl011_uart_hw_init(plat, 0, 1);
	if (ret < 0)
		return ret;
	regs[REG_UART_CR(plat->base)] |= TXE;
	return ret;
}

static int pl011_uart_init(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;
	int ret = 0;
	const struct pl011_uart_platform_data *plat = dev->platform_data;
	struct pl011_uart_priv *priv;

	if (!plat)
		return -EINVAL;

	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	ret = pl011_uart_hw_init(plat, 0, 0);
	if (ret < 0)
		goto error;
	priv->platform_data = plat;
	pipe->dev_data = bathos_dev_init(&pl011_ll_uart_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENODEV;
		goto error;
	}
	return 0;

error:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}

static int pl011_uart_open(struct bathos_pipe *pipe)
{
	int stat = 0;

	stat = pl011_uart_init(pipe);
	if (stat)
		return stat;
	return bathos_dev_open(pipe);
}

const struct bathos_dev_ops pl011_uart_dev_ops = {
	.open = pl011_uart_open,
	.read = bathos_dev_read,
	.write = bathos_dev_write,
	.close = bathos_dev_close,
	.ioctl = bathos_dev_ioctl,
};
