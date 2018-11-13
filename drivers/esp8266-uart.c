/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#include <ets_sys.h>
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
#include <bathos/esp8266-uart.h>
#include <bathos/bitops.h>

/* Registers' offset */
#define UART_FIFO	0x00
#define UART_INT_RAW	0x04
#define UART_INT_ST	0x08
# define FRAMING_ERROR  BIT(3)
# define OVERRUN_ERROR  BIT(4)

#define UART_INT_ENA	0x0c
# define UART_RXFIFO_FULL_INT_ENA BIT(0)
#define UART_INT_CLR	0x10
#define UART_CLKDIV	0x14
#define UART_AUTOBAUD	0x18
#define UART_STATUS	0x1c
#define UART_CONF0	0x20
# define UART_RXFIFO_RST BIT(17)
# define UART_TXFIFO_RST BIT(18)
#define UART_CONF1	0x24
#define UART_LOWPULSE	0x28
#define UART_HIGHPULSE	0x2c
#define UART_PULSE_NUM	0x30
#define UART_DATE	0x78
#define UART_ID		0x7c

struct esp8266_uart_priv {
	struct bathos_dev_data *dev_data;
	const struct esp8266_uart_platform_data *plat;
	int tx_enabled;
};

static int esp8266_uart_rx_enable(void *_priv)
{
	struct esp8266_uart_priv *priv = _priv;
	const struct esp8266_uart_platform_data *plat = priv->plat;
	uint32_t v = readl(plat->base + UART_INT_ENA);

	/* Just enable rx interrupt */
	v |= UART_RXFIFO_FULL_INT_ENA;
	writel(v, plat->base + UART_INT_ENA);
	return 0;
}

static int esp8266_uart_tx_enable(void *_priv)
{
	struct esp8266_uart_priv *priv = _priv;

	priv->tx_enabled = 1;
	return 0;
}

static int esp8266_uart_rx_disable(void *_priv)
{
	struct esp8266_uart_priv *priv = _priv;
	const struct esp8266_uart_platform_data *plat = priv->plat;
	uint32_t v = readl(plat->base + UART_INT_ENA);

	/* Just disable rx interrupt */
	v &= ~UART_RXFIFO_FULL_INT_ENA;
	writel(v, plat->base + UART_INT_ENA);
	return 0;
}

static int esp8266_uart_tx_disable(void *_priv)
{
	struct esp8266_uart_priv *priv = _priv;
	
	priv->tx_enabled = 0;
	return 0;
}

static inline unsigned int get_tx_fifo_cnt(unsigned long base)
{
	return (readl(base + UART_STATUS) >> 16) & 0xffUL;
}

static int _sync_putc(uint32_t base, const char c)
{
	while (get_tx_fifo_cnt(base) >= 126UL);
	writel(c, base + UART_FIFO);
	return 1;
}

static int esp8266_uart_putc(void *_priv, const char c)
{
	struct esp8266_uart_priv *priv = _priv;
	const struct esp8266_uart_platform_data *plat = priv->plat;

	if (priv->tx_enabled)
		return -EPERM;
	return _sync_putc(plat->base, c);
}

const struct bathos_ll_dev_ops esp8266_ll_uart_dev_ops = {
	.putc = esp8266_uart_putc,
	.rx_disable = esp8266_uart_rx_disable,
	.rx_enable = esp8266_uart_rx_enable,
	.tx_disable = esp8266_uart_tx_disable,
	.tx_enable = esp8266_uart_tx_enable,
};

static inline unsigned int get_rx_fifo_cnt(unsigned long base)
{
	return readl(base + UART_STATUS) & 0xffUL;
}

void esp8266_uart_irq_handler(struct bathos_dev *dev)
{
	const struct esp8266_uart_platform_data *plat = dev->platform_data;
	uint32_t base = plat->base, status;

	status = readl(base + UART_INT_ST);
	if ((status & FRAMING_ERROR) || (status & OVERRUN_ERROR)) {
		pr_debug("%s: ERROR (0x%08x)\n", __func__, status);
		goto end;
	}
	bathos_dev_push_chars(dev, (void *)(base + UART_FIFO),
			      get_rx_fifo_cnt(base));
end:
	writel(status, base + UART_INT_CLR);
}

static int
esp8266_uart_set_baudrate(const struct esp8266_uart_platform_data *plat,
			   uint32_t baud)
{
	uart_div_modify(plat->index, UART_CLK_FREQ / baud);
	return 0;
}

int esp8266_uart_hw_init(const struct esp8266_uart_platform_data *plat)
{
	uint32_t v;

	esp8266_uart_set_baudrate(plat, 115200);
	if (plat->pin_setup)
		plat->pin_setup(plat);
	/* Reset FIFO */
	v = readl(plat->base + UART_CONF0);
	v |= (UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, plat->base + UART_CONF0);
	v &= ~(UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, plat->base + UART_CONF0);
	/* Set rx fifo threshold */
	v = readl(plat->base + UART_CONF1);
	v &= ~0x7f;
	v |= (plat->rx_fifo_threshold & 0x7f);
	writel(v, plat->base + UART_CONF1);
	return 0;
}

int esp8266_uart_init(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;
	const struct esp8266_uart_platform_data *plat = dev->platform_data;
	struct esp8266_uart_priv *priv;
	int ret = 0;

	if (!dev->platform_data)
		return -EINVAL;
	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	priv->plat = plat;
	ret = esp8266_uart_hw_init(dev->platform_data);
	if (ret < 0)
		return ret;
	pipe->dev_data = bathos_dev_init(&esp8266_ll_uart_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENOMEM;
		goto error;
	}
	return 0;

error:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}

static int esp8266_uart_open(struct bathos_pipe *pipe)
{
	int stat = 0;

	stat = esp8266_uart_init(pipe);
	if (stat)
		return stat;
	ETS_UART_INTR_ATTACH(esp8266_uart_irq_handler,  pipe->dev);
	return bathos_dev_open(pipe);
}


int esp8266_uart_console_init(const struct esp8266_uart_platform_data *plat)
{
	int ret;

	ret = esp8266_uart_hw_init(plat);
	if (ret < 0)
		return ret;
	return 0;
}

void esp8266_uart_console_putc(const struct esp8266_uart_platform_data *plat,
				const char c)
{
	(void)_sync_putc(plat->base, c);
}

const struct bathos_dev_ops esp8266_uart_dev_ops = {
	.open = esp8266_uart_open,
	.read = bathos_dev_read,
	.write = bathos_dev_write,
	.close = bathos_dev_close,
	.ioctl = bathos_dev_ioctl,
};
