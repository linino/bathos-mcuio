/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

/*
 * Basic driver for nrf5x radio, just sends and receives packets. No collision
 * avoidance is performed at this level. Pipe interface is provided.
 * Radio is always in rx mode until a packet is written via write. As soon as
 * tx has been completed, radio is switched back to rx mode.
 * If an empty packet is received, a radio_empty_packet_received is triggered.
 *
 * Events: pipe_input_ready, radio_empty_packet_received
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
#include <bathos/nrf5x-radio.h>
#include <bathos/allocator.h>
#include <bathos/bitops.h>


#define RADIO_TASK_TXEN		0x000
#define RADIO_TASK_RXEN		0x004
#define RADIO_TASK_START	0x008
#define RADIO_TASK_STOP		0x00c
#define RADIO_TASK_DISABLE	0x010
#define RADIO_TASK_RSSISTART	0x014
#define RADIO_TASK_RSSISTOP	0x018
#define RADIO_TASK_BCSTART	0x01c
#define RADIO_TASK_BCSTOP	0x020

#define RADIO_EVENT_READY	0x100
#define RADIO_EVENT_ADDRESS	0x104
#define RADIO_EVENT_PAYLOAD	0x108
#define RADIO_EVENT_END		0x10c
#define RADIO_EVENT_DISABLED	0x110
#define RADIO_EVENT_DEVMATCH	0x114
#define RADIO_EVENT_DEVMISS	0x118
#define RADIO_EVENT_RSSIEND	0x11c
#define RADIO_EVENT_BCMATCH	0x128

#define RADIO_REG_SHORTS	0x200
#define RADIO_REG_INTENSET	0x304
#define RADIO_REG_INTENCLR	0x308
#define RADIO_REG_CRCSTATUS	0x400
#define RADIO_REG_RXMATCH	0x408
#define RADIO_REG_RXCRC		0x40c
#define RADIO_REG_DAI		0x410
#define RADIO_REG_PACKETPTR	0x504
#define RADIO_REG_FREQUENCY	0x508
#define RADIO_REG_TXPOWER	0x50c
#define RADIO_REG_MODE		0x510
#define RADIO_REG_PCNF0		0x514
#define RADIO_REG_PCNF1		0x518
#define RADIO_REG_BASE(a)	(0x51c + (a)*4)
/* a can range from 0 to 7, we do 8 bits accesses */
#define RADIO_REG_PREFIX(a)	(0x524 + (a))
#define RADIO_REG_TXADDRESS	0x52c
#define RADIO_REG_RXADDRESSES	0x530
#define RADIO_REG_CRCCNF	0x534
#define RADIO_REG_CRCPOLY	0x538
#define RADIO_REG_CRCINIT	0x53c
#define RADIO_REG_RSSISAMPLE	0x548
#define RADIO_REG_STATE		0x550
#define RADIO_REG_BCC		0x560
#define RADIO_REG_DAB(a)	(0x600 + (a) * 4)
#define RADIO_REG_DAP(a)	(0x620 + (a) * 4)
#define RADIO_REG_DACNF		0x640
#define RADIO_REG_POWER		0xffc

enum radio_status {
	idle = 0,
	rx = 1,
	tx = 2,
};

enum nrf5x_radio_state {
	nrf5x_disabled = 0,
	nrf5x_rxru = 1,
	nrf5x_rxidle = 2,
	nrf5x_rx = 3,
	nrf5x_rxdisable = 4,
	nrf5x_txru = 9,
	nrf5x_txidle = 10,
	nrf5x_tx = 11,
	nrf5x_txdisable = 12,
};

struct nrf5x_radio_priv {
	struct bathos_dev_data *dev_data;
#define RADIO_TX_ENABLED 1
#define RADIO_RX_ENABLED 2
	int flags;
	enum radio_status status;
	const struct nrf5x_radio_platform_data *platform_data;
};

declare_event(radio_empty_packet_received);

static void __radio_clear_events(struct nrf5x_radio_priv *priv)
{
	const struct nrf5x_radio_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;
	int offs;

	for (offs = RADIO_EVENT_READY; offs <= RADIO_EVENT_RSSIEND; offs += 4)
		writel(0, base + offs);
	writel(0, base + RADIO_EVENT_BCMATCH);
}

static void __radio_stop(struct nrf5x_radio_priv *priv)
{
	const struct nrf5x_radio_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	if (priv->status == idle)
		return;
	/* Disable radio */
	writel(1, base + RADIO_TASK_DISABLE);
	/* Wait for radio disabled */
	while (readl(base + RADIO_REG_STATE) != nrf5x_disabled);
	priv->status = idle;
}

static void __radio_start_rx(struct nrf5x_radio_priv *priv)
{
	const struct nrf5x_radio_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;

	__radio_stop(priv);

	/* (Re-)enable interrupt on packet end */
	writel(BIT(3), base + RADIO_REG_INTENSET);
	/* Enable rx */
	writel(1, base + RADIO_TASK_RXEN);
	/* Wait for radio ready */
	while (readl(base + RADIO_REG_STATE) != nrf5x_rxidle);
	/* Start receiving */
	writel(1, base + RADIO_TASK_START);
	priv->status = rx;
}

static int nrf5x_radio_rx_enable(void *_priv)
{
	struct nrf5x_radio_priv *priv = (struct nrf5x_radio_priv*)_priv;
	const struct nrf5x_radio_platform_data *plat = priv->platform_data;

	priv->flags |= RADIO_RX_ENABLED;
	__radio_start_rx(priv);
	bathos_enable_irq(plat->irq);
	return 0;
}

static int nrf5x_radio_tx_enable(void *_priv)
{
	struct nrf5x_radio_priv *priv = (struct nrf5x_radio_priv *)_priv;

	/* Software tx enable, hw enable is actually done on tx start */
	priv->flags |= RADIO_TX_ENABLED;
	return 0;
}

static int nrf5x_radio_rx_disable(void *_priv)
{
	struct nrf5x_radio_priv *priv = (struct nrf5x_radio_priv*)_priv;
	const struct nrf5x_radio_platform_data *plat = priv->platform_data;

	priv->flags &= ~RADIO_RX_ENABLED;
	bathos_disable_irq(plat->irq);
	if (priv->status == rx)
		__radio_stop(priv);
	return 0;
}

static int nrf5x_radio_tx_disable(void *_priv)
{
	struct nrf5x_radio_priv *priv = (struct nrf5x_radio_priv*)_priv;

	priv->flags &= ~RADIO_TX_ENABLED;
	if (priv->status == tx)
		__radio_stop(priv);
	return 0;
}

static int nrf5x_radio_write(void *_priv, const char *buf, int len)
{
	struct nrf5x_radio_priv *priv = _priv;
	const struct nrf5x_radio_platform_data *plat = priv->platform_data;
	uint32_t base = plat->base;
	int l;

	if (!(priv->flags & RADIO_TX_ENABLED))
		return -EPERM;

	if (priv->status == tx)
		/* This should never happen, we do tx synchronously */
		return -EAGAIN;

	l = min(len, plat->packet_size);

	/* Copy buffer and setup packet pointer */
	if (buf)
		memcpy(plat->packet_area->payload, buf, l);
	writel((uint32_t)plat->packet_area, base + RADIO_REG_PACKETPTR);

	/* Setup length */
	plat->packet_area->len = l;

	/* Disable radio */
	__radio_stop(priv);

	/* Disable interrupt on packet end */
	writel(BIT(3), base + RADIO_REG_INTENCLR);

	/* Enable tx */
	writel(1, base + RADIO_TASK_TXEN);

	/* Wait for radio ready */
	while (readl(base + RADIO_REG_STATE) != nrf5x_txidle);

	priv->status = tx;

	/* Start transmitting */
	writel(1, base + RADIO_TASK_START);

	/* Wait for tx done */
	while (!readl(base + RADIO_EVENT_END));

	/* Clear events */
	writel(0, base + RADIO_EVENT_ADDRESS);
	writel(0, base + RADIO_EVENT_PAYLOAD);
	writel(0, base + RADIO_EVENT_END);

	__radio_start_rx(priv);
	return l;
}

void nrf5x_radio_irq_handler(struct bathos_dev *dev)
{
	const struct nrf5x_radio_platform_data *plat = dev->platform_data;
	uint32_t base = plat->base;

	if (!readl(base + RADIO_EVENT_END))
		return;

	if (!plat->packet_area->len) {
		/* Empty packet received */
		pipe_dev_trigger_event(dev, &evt_radio_empty_packet_received);
		goto end;
	}

	(void)bathos_dev_push_chars(dev, plat->packet_area->payload,
				    min(plat->packet_size,
					plat->packet_area->len));
end:
	/* Clear END events */
	writel(0, base + RADIO_EVENT_ADDRESS);
	writel(0, base + RADIO_EVENT_PAYLOAD);
	writel(0, base + RADIO_EVENT_END);

	/* Restart rx, radio is in rx idle at this point */
	writel(1, base + RADIO_TASK_START);
}

const struct bathos_ll_dev_ops nrf5x_ll_radio_dev_ops = {
	/* We have our own write() */
	.putc = NULL,
	.write = nrf5x_radio_write,
	.rx_disable = nrf5x_radio_rx_disable,
	.rx_enable = nrf5x_radio_rx_enable,
	.tx_disable = nrf5x_radio_tx_disable,
	.tx_enable = nrf5x_radio_tx_enable,
};

static int nrf5x_radio_init(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;
	const struct nrf5x_radio_platform_data *plat = dev->platform_data;
	struct nrf5x_radio_priv *priv;
	int i;
	uint32_t base, tmp;

	if (!plat)
		return -EINVAL;

	if (plat->addr_length < 1 || plat->addr_length > 5)
		return -EINVAL;


	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->platform_data = plat;
	priv->flags = 0;
	priv->status = idle;
	base = plat->base;

	pipe->dev_data = bathos_dev_init(dev, &nrf5x_ll_radio_dev_ops, priv);
	if (!pipe->dev_data) {
		bathos_free_buffer(priv, sizeof(*priv));
		return -ENOMEM;
	}

	/*
	   Power on peripheral, setup registers, do not enable rx,
	   that will be done later on
	*/
	writel(1, base + RADIO_REG_POWER);
	/*
	  We just use length (8 bits on air),
	  no need for S0 and S1.
	*/
	writel(8, base + RADIO_REG_PCNF0);
	/*
	 * MAXLEN = packet_size
	 * STATLEN = 0
	 * BALEN = addr_length - 1
	 */
	writel(plat->packet_size | (plat->addr_length - 1) << 16,
	       base + RADIO_REG_PCNF1);
	/* Setup address 0 */
	writeb(plat->my_addr[0], base + RADIO_REG_PREFIX(0));
	for (i = 1, tmp = 0; i < plat->addr_length; i++)
	    tmp |= plat->my_addr[i] << (8*(i - 1));
	writel(tmp, base + RADIO_REG_BASE(0));
	/* Setup address 1 */
	writeb(plat->dst_addr[0], base + RADIO_REG_PREFIX(1));
	for (i = 1, tmp = 0; i < plat->addr_length; i++)
		tmp |= plat->dst_addr[i] << (8*(i - 1));
	writel(tmp, base + RADIO_REG_BASE(1));
	/* Select logical address 0 for rx */
	writel(BIT(0), base + RADIO_REG_RXADDRESSES);
	/* And logical address 0 for tx */
	writel(0, base + RADIO_REG_TXADDRESS);
	/* Enable interrupt on packet end */
	writel(BIT(3), base + RADIO_REG_INTENSET);
	memset(plat->packet_area, 0, plat->packet_size);
	/* Set packet pointer */
	writel((uint32_t)plat->packet_area, base + RADIO_REG_PACKETPTR);
	/* Clear all events */
	__radio_clear_events(priv);
	return 0;
}


static int nrf5x_radio_open(struct bathos_pipe *pipe)
{
	int stat = 0;

	stat = nrf5x_radio_init(pipe);
	if (stat)
		return stat;
	return bathos_dev_open(pipe);
}

const struct bathos_dev_ops nrf5x_radio_dev_ops = {
	.open = nrf5x_radio_open,
	.read = bathos_dev_read,
	.write = bathos_dev_write,
	.close = bathos_dev_close,
	.ioctl = bathos_dev_ioctl,
};
