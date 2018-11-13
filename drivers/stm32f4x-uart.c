/*
 * Copyright (c) dog hunter AG - Zug - CH
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
#include <bathos/stm32f4x-uart.h>
#include <bathos/allocator.h>
#include <bathos/bitops.h>
#include <family/clocks.h>

#define USART_SR		0x0
# define TXE			BIT(7)
# define TC			BIT(6)
# define RXNE			BIT(5)
# define ORE			BIT(3)
# define FE			BIT(1)

#define USART_DR		0x4
#define USART_BRR		0x8
#define USART_CR1		0xc
# define OVER8			BIT(15)
# define UE			BIT(13)
# define M			BIT(12)
# define PCE			BIT(10)
# define PS			BIT(9)
# define PEIE			BIT(8)
# define TXEIE			BIT(7)
# define TCIE			BIT(6)
# define RXNEIE			BIT(5)
# define TE			BIT(3)
# define RE			BIT(2)

#define USART_CR2		0x10
#define USART_CR3		0x14
#define USART_GTPR		0x18

struct stm32f4x_uart_priv {
	struct bathos_dev_data *dev_data;
	const struct stm32f4x_uart_platform_data *platform_data;
};

static int stm32f4x_uart_rx_enable(void *_priv)
{
	struct stm32f4x_uart_priv *priv = (struct stm32f4x_uart_priv*)_priv;
	const struct stm32f4x_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base, tmp;

	tmp = readl(base + USART_CR1);
	tmp |= (RE | RXNEIE);
	writel(tmp, base + USART_CR1);
	bathos_enable_irq(plat->irq);
	return 0;
}

static int _tx_enable(const struct stm32f4x_uart_platform_data *plat)
{
	uint32_t base = plat->base, tmp;

	tmp = readl(base + USART_CR1);
	tmp |= TE;
	writel(tmp, base + USART_CR1);
	return 0;
}

static int stm32f4x_uart_tx_enable(void *_priv)
{
	struct stm32f4x_uart_priv *priv = (struct stm32f4x_uart_priv*)_priv;
	const struct stm32f4x_uart_platform_data *plat = priv->platform_data;

	return _tx_enable(plat);
}

static int stm32f4x_uart_rx_disable(void *_priv)
{
	struct stm32f4x_uart_priv *priv = (struct stm32f4x_uart_priv*)_priv;
	const struct stm32f4x_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base, tmp;

	tmp = readl(base + USART_CR1);
	tmp &= ~(RE | RXNEIE);
	writel(tmp, base + USART_CR1);
	bathos_disable_irq(plat->irq);
	return 0;
}

static int stm32f4x_uart_tx_disable(void *_priv)
{
	struct stm32f4x_uart_priv *priv = (struct stm32f4x_uart_priv*)_priv;
	const struct stm32f4x_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base, tmp;

	tmp = readl(base + USART_CR1);
	tmp &= ~TE;
	writel(tmp, base + USART_CR1);
	return 0;
}

static int _sync_putc(uint32_t base, const char c)
{
	while (!(readl(base + USART_SR) & TXE));
	writel(c, base + USART_DR);
	return 1;
}

static int stm32f4x_uart_putc(void *_priv, const char c)
{
	struct stm32f4x_uart_priv *priv = (struct stm32f4x_uart_priv*)_priv;
	const struct stm32f4x_uart_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	return _sync_putc(base, c);
}

const struct bathos_ll_dev_ops stm32f4x_ll_uart_dev_ops = {
	.putc = stm32f4x_uart_putc,
	.rx_disable = stm32f4x_uart_rx_disable,
	.rx_enable = stm32f4x_uart_rx_enable,
	.tx_disable = stm32f4x_uart_tx_disable,
	.tx_enable = stm32f4x_uart_tx_enable,
};

void stm32f4x_uart_irq_handler(struct bathos_dev *dev)
{
	const struct stm32f4x_uart_platform_data *plat = dev->platform_data;
	uint32_t base = plat->base;
	char c = readl(base + USART_DR);

	(void)bathos_dev_push_chars(dev, &c, 1);
}

static int
stm32f4x_uart_set_baudrate(const struct stm32f4x_uart_platform_data *plat,
			   uint32_t baud)
{
	uint32_t base = plat->base;
	uint32_t tmp;
	uint32_t div_fraction, div_mantissa;

	/* 16 samples per bit */
	tmp = (plat->clock >> 4) * 100 / baud;
	div_fraction = ((tmp % 100) << 4) / 100;
	div_mantissa = tmp / 100;
	writel(div_fraction | (div_mantissa << 4), base + USART_BRR);
	return 0;
}

int stm32f4x_uart_hw_init(const struct stm32f4x_uart_platform_data *plat)
{
	int ret;
	uint32_t base = plat->base;

	/* Enable peripheral clock */
	ret = stm32f4x_enable_peripheral_clock(plat->bus, plat->periph_id);
	if (ret < 0)
		return ret;
	/* Setup rx and rx pins */
	gpio_dir_af(plat->rx_pin, 0, 1, plat->rx_af);
	gpio_dir_af(plat->tx_pin, 1, 1, plat->tx_af);
	/* Setup baud rate generator */
	stm32f4x_uart_set_baudrate(plat, 115200);
	/* Enable uart */
	writel(UE, base + USART_CR1);
	return 0;
}

int stm32f4x_uart_init(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;
	const struct stm32f4x_uart_platform_data *plat = dev->platform_data;
	struct stm32f4x_uart_priv *priv;
	int ret = 0;

	if (!dev->platform_data)
		return -EINVAL;
	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	priv->platform_data = plat;
	ret = stm32f4x_uart_hw_init(dev->platform_data);
	if (ret < 0)
		return ret;
	pipe->dev_data = bathos_dev_init(dev, &stm32f4x_ll_uart_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENOMEM;
		goto error;
	}
	return 0;

error:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}

static int stm32f4x_uart_open(struct bathos_pipe *pipe)
{
	int stat = 0;

	stat = stm32f4x_uart_init(pipe->dev);
	if (stat)
		return stat;
	return bathos_dev_open(pipe);
}


int stm32f4x_uart_console_init(const struct stm32f4x_uart_platform_data *plat)
{
	int ret;

	ret = stm32f4x_uart_hw_init(plat);
	if (ret < 0)
		return ret;
	return _tx_enable(plat);
}

void stm32f4x_uart_console_putc(const struct stm32f4x_uart_platform_data *plat,
				const char c)
{
	(void)_sync_putc(plat->base, c);
}

const struct bathos_dev_ops stm32f4x_uart_dev_ops = {
	.open = stm32f4x_uart_open,
	.read = bathos_dev_read,
	.write = bathos_dev_write,
	.close = bathos_dev_close,
	.ioctl = bathos_dev_ioctl,
};
