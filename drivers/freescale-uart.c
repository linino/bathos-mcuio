/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>, original code by
 * Aurelio Colosimo <aurelio@aureliocolosimo.it>
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
#include <bathos/freescale-uart.h>

#define REG_UART_BDH(b)		(b)
#define REG_UART_BDL(b)		((b) + 1)
#define REG_UART_C1(b)		((b) + 2)

#define REG_UART_C2(b)		((b) + 3)
#define UART_C2_TIE_MASK	0x80
#define UART_C2_TCIE_MASK	0x40
#define UART_C2_RIE_MASK	0x20
#define UART_C2_ILIE_MASK	0x10
#define UART_C2_TE_MASK		0x08
#define UART_C2_RE_MASK		0x04
#define UART_C2_RWU_MASK	0x02
#define UART_C2_SBK_MASK	0x01

#define REG_UART_S1(b)		((b) + 4)
#define UART_S1_TDRE_MASK	0x80
#define UART_S1_TC_MASK		0x40
#define UART_S1_RDRF_MASK	0x20
#define UART_S1_IDLE_MASK	0x10
#define UART_S1_OR_MASK		0x08
#define UART_S1_NF_MASK		0x04
#define UART_S1_FE_MASK		0x02
#define UART_S1_PF_MASK		0x01

#define REG_UART_S2(b)		((b) + 5)
#define REG_UART_C3(b)		((b) + 6)
#define REG_UART_D(b)		((b) + 7)

struct freescale_uart_priv {
	const struct freescale_uart_platform_data *platform_data;
	struct bathos_dev_data *dev_data;
};

static int freescale_uart_rx_enable(void *_priv)
{
	struct freescale_uart_priv *priv = (struct freescale_uart_priv*)_priv;
	const struct freescale_uart_platform_data *plat = priv->platform_data;
	volatile uint8_t *regs8 = (void*)regs;
	uint32_t base = plat->base;

	bathos_enable_irq(plat->irq);
	regs8[REG_UART_C2(base)] |= (UART_C2_RIE_MASK | UART_C2_RE_MASK);
	return 0;
}

static int freescale_uart_tx_enable(void *_priv)
{
	struct freescale_uart_priv *priv = (struct freescale_uart_priv*)_priv;
	const struct freescale_uart_platform_data *plat = priv->platform_data;
	volatile uint8_t *regs8 = (void*)regs;
	uint32_t base = plat->base;

	regs8[REG_UART_C2(base)] |= UART_C2_TE_MASK;
	return 0;
}

static int freescale_uart_rx_disable(void *_priv)
{
	struct freescale_uart_priv *priv = (struct freescale_uart_priv*)_priv;
	const struct freescale_uart_platform_data *plat = priv->platform_data;
	volatile uint8_t *regs8 = (void*)regs;
	uint32_t base = plat->base;

	regs8[REG_UART_C2(base)] &= ~((UART_C2_RIE_MASK | UART_C2_RE_MASK));
	bathos_disable_irq(plat->irq);
	return 0;
}

static int freescale_uart_tx_disable(void *_priv)
{
	struct freescale_uart_priv *priv = (struct freescale_uart_priv*)_priv;
	const struct freescale_uart_platform_data *plat = priv->platform_data;
	volatile uint8_t *regs8 = (void*)regs;
	uint32_t base = plat->base;

	regs8[REG_UART_C2(base)] &= ~UART_C2_TE_MASK;
	return 0;
}

void freescale_uart_console_putc(uint32_t base, const char c)
{
	volatile uint8_t *regs8 = (void*)regs;

	while(!(regs8[REG_UART_S1(base)] & UART_S1_TDRE_MASK));
	regs8[REG_UART_D(base)] = c;
}

static int freescale_uart_putc(void *_priv, const char c)
{
	struct freescale_uart_priv *priv = (struct freescale_uart_priv*)_priv;
	const struct freescale_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	freescale_uart_console_putc(base, c);
	return 1;
}

const struct bathos_ll_dev_ops freescale_ll_uart_dev_ops = {
	.putc = freescale_uart_putc,
	.rx_disable = freescale_uart_rx_disable,
	.rx_enable = freescale_uart_rx_enable,
	.tx_disable = freescale_uart_tx_disable,
	.tx_enable = freescale_uart_tx_enable,
};

void freescale_uart_irq_handler(struct bathos_dev *dev)
{
	volatile uint8_t *regs8 = (void*)regs;
	const struct freescale_uart_platform_data *plat = dev->platform_data;
	uint32_t base = plat->base;
	char c;
	uint8_t s1;

	s1 = regs8[REG_UART_S1(base)];
	if ((s1 & UART_S1_RDRF_MASK) == 0)
		/* Spurious irq ? */
		return ;
	c = regs8[REG_UART_D(base)];
	(void)bathos_dev_push_chars(dev, &c, 1);
}

static int freescale_uart_set_baudrate(uint32_t base,
				       int clock,
				       uint32_t baud)
{
	/* set BDH and BDL */
	uint32_t bd = clock / 16 / baud;
	volatile uint8_t *regs8 = (void*)regs;

	regs8[REG_UART_BDH(base)] &= ~(0x1f);
	regs8[REG_UART_BDH(base)] |= (bd >> 8) & 0x1f;
	regs8[REG_UART_BDL(base)] = bd & 0xff;
	return 0;
}

static int
freescale_uart_hw_init(const struct freescale_uart_platform_data *plat,
		       int enable_rx, int enable_tx)
{
	int ret = 0;
	volatile uint8_t *regs8 = (void*)regs;

	if (plat->power)
		ret = plat->power(plat, 1);
	if (ret < 0)
		return ret;
	/* Setup rx and tx pins */
	gpio_dir_af(plat->rx_pin, 0, 1, plat->afnum);
	gpio_dir_af(plat->tx_pin, 1, 1, plat->afnum);
	ret = freescale_uart_set_baudrate(plat->base, plat->clock, 115200);
	if (ret < 0)
		return ret;
	if (enable_tx)
		regs8[REG_UART_C2(plat->base)] |= UART_C2_TE_MASK;
	if (enable_rx)
		regs8[REG_UART_C2(plat->base)] |=
			(UART_C2_RIE_MASK | UART_C2_RE_MASK);
	return ret;
}

int freescale_uart_console_init(const struct freescale_uart_platform_data *plat)
{
	return freescale_uart_hw_init(plat, 0, 1);
}

static int freescale_uart_init(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;
	int ret = 0;
	const struct freescale_uart_platform_data *plat = dev->platform_data;
	struct freescale_uart_priv *priv;

	if (!plat)
		return -EINVAL;

	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	ret = freescale_uart_hw_init(plat, 0, 0);
	if (ret < 0)
		goto error;
	priv->platform_data = plat;
	pipe->dev_data = bathos_dev_init(dev, &freescale_ll_uart_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENODEV;
		goto error;
	}
	return 0;

error:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}

static int freescale_uart_open(struct bathos_pipe *pipe)
{
	int stat = 0;

	stat = freescale_uart_init(pipe);
	if (stat)
		return stat;
	return bathos_dev_open(pipe);
}

const struct bathos_dev_ops freescale_uart_dev_ops = {
	.open = freescale_uart_open,
	.read = bathos_dev_read,
	.write = bathos_dev_write,
	.close = bathos_dev_close,
	.ioctl = bathos_dev_ioctl,
};
