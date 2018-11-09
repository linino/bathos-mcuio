/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* esp8266 spi master driver */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/esp8266-spim.h>
#include <bathos/buffer_queue_server.h>
#include <bathos/io.h>
#include <ets_sys.h>
#include <osapi.h>
#include <user_interface.h>

/* Registers' offset */
#define SPI_CMD		0x00
#  define SPI_BUSY	BIT(18)

#define SPI_ADDR	0x04
#define SPI_CTRL	0x08
#define SPI_RD_STATUS	0x10
#define SPI_CTRL2	0x14
#define SPI_CLOCK	0x18
#  define SPI_CLOCK_PRESCALER_SHIFT 18
#  define SPI_CLOCK_DIVIDER_SHIFT 12
#define SPI_USER	0x1c
#  define COMMAND_EN	BIT(31)
#  define ADDR_EN	BIT(30)
#  define DUMMY_EN	BIT(29)
#  define MISO_EN	BIT(28)
#  define MOSI_EN	BIT(27)
#  define DUMMY_IDLE	BIT(26)
#  define MOSIH		BIT(25)
#  define MISOH		BIT(24)
#define SPI_USER1	0x20
#  define MOSI_BITLEN_MASK 0x1ff
#  define MOSI_BITLEN_SHIFT 17
#  define MISO_BITLEN_MASK 0x1ff
#  define MISO_BITLEN_SHIFT 8

#define SPI_USER2	0x24
#define SPI_PIN		0x2c
#define SPI_SLAVE	0x30
#  define SYNC_RESET	BIT(31)
#  define SLAVE_MODE	BIT(30)
#  define WR_RD_BUF_EN	BIT(29)
#  define WR_RD_STA_EN  BIT(28)
#  define ALL_INTERRUPTS 0x3e0

#define SPI_W(j)	(0x40 + (j))

struct esp8266_spim_priv {
	struct bathos_pipe *pipe;
	const struct esp8266_spim_platform_data *plat;
	struct list_head rx_queue;
	struct list_head tx_queue;
	void *buffer_area;
	struct bathos_dev_data *dev_data;
};

declare_event(esp8266_spim_setup);
declare_event(esp8266_spim_done);

static void start_tx(struct esp8266_spim_priv *priv)
{
	/* FIXME: IMPLEMENT THIS */
}

static int _spim_init(struct esp8266_spim_priv *priv)
{
	const struct esp8266_spim_platform_data *plat = priv->plat;
	uint32_t v;

	/* MSB first */
	writel(0, plat->base + SPI_CTRL);
	/* Master mode */
	v = readl(SPI_SLAVE + plat->base);
	v &= ~SLAVE_MODE;
	v |= WR_RD_BUF_EN | ALL_INTERRUPTS;
	writel(v, plat->base + SPI_SLAVE);
	/* 32 bytes mode */
	v = readl(plat->base + SPI_USER1);
	v &= ~((MOSI_BITLEN_MASK << MOSI_BITLEN_SHIFT) |
	       (MISO_BITLEN_MASK << MISO_BITLEN_SHIFT));
	v |= ((31 << MOSI_BITLEN_SHIFT) | (31 << MISO_BITLEN_SHIFT));
	writel(v, plat->base + SPI_USER1);
	/* 8MHz, divider shall be 10: prescaler 2, divider 5 */
	writel(1 << SPI_CLOCK_PRESCALER_SHIFT |
	       4 << SPI_CLOCK_DIVIDER_SHIFT |
	       ((7 + 1) / 2), plat->base + SPI_CLOCK);

	if (plat->setup_pins)
		return plat->setup_pins(plat->instance);

	return 0;
}

/*
 * This is invoked when a buffer has been setup by the client and is
 * ready for tx/rx submission
 */
static void esp8266_spim_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;
	struct bathos_bqueue *q;
	struct esp8266_spim_priv *priv;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	op = to_operation(b);
	q = b->queue;
	priv = bathos_bqueue_to_ll_priv(q);
	if (!priv) {
		printf("%s: ERR, priv is NULL\n", __func__);
		return;
	}

	switch (op->type) {
	case NONE:
		printf("%s: WARN: operation is NONE\n", __func__);
		bathos_bqueue_server_buf_done(b);
		break;
	case SEND:
		/* Move buffer to tail of tx queue and start a transmission */
		list_move_tail(&b->list, &priv->tx_queue);
		start_tx(priv);
		break;
	case RECV:
		/*
		 * Move buffer to tail of rx queue (waiting for buffer to be
		 * used)
		 */
		list_move_tail(&b->list, &priv->rx_queue);
		break;
	default:
		printf("%s: ERR: invalid operation type\n", __func__);
		bathos_bqueue_server_buf_done(b);
		break;
	}
}
declare_event_handler(esp8266_spim_setup, NULL, esp8266_spim_setup_handler,
		      NULL);


/*
 * This is invoked when a buffer has been released by the client
 */
static void esp8266_spim_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(esp8266_spim_done, NULL, esp8266_spim_done_handler,
		      NULL);

static int esp8266_spim_async_open(void *_priv)
{
	int ret;
	struct esp8266_spim_priv *priv = _priv;
	const struct esp8266_spim_platform_data *plat = priv->plat;
	struct bathos_dev *dev = priv->pipe->dev;
	struct bathos_bqueue *q = dev->ops->get_bqueue ?
	    dev->ops->get_bqueue(priv->pipe) : NULL;

	if (!q)
		return -EINVAL;
	ret = bathos_bqueue_server_init(q,
					&event_name(esp8266_spim_setup),
					&event_name(esp8266_spim_done),
					priv->buffer_area,
					plat->nbufs,
					plat->bufsize,
					DONTCARE);
	if (ret)
		return ret;

	return _spim_init(priv);
}

static const struct bathos_ll_dev_ops esp8266_spim_ll_dev_ops = {
	.async_open = esp8266_spim_async_open,
};


static int esp8266_spim_open(struct bathos_pipe *pipe)
{
	struct esp8266_spim_priv *priv;
	struct bathos_dev *dev = pipe->dev;
	const struct esp8266_spim_platform_data *plat = dev->platform_data;
	int ret;

	if (!pipe_mode_is_async(pipe))
		return -EINVAL;

	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->plat = plat;
	priv->pipe = pipe;
	INIT_LIST_HEAD(&priv->rx_queue);
	INIT_LIST_HEAD(&priv->tx_queue);
	priv->buffer_area = bathos_alloc_buffer(plat->nbufs * plat->bufsize);
	if (!priv->buffer_area) {
		ret = -ENOMEM;
		goto error0;
	}
	priv->dev_data = bathos_dev_init(&esp8266_spim_ll_dev_ops, priv);
	if (!priv->dev_data) {
		ret = -ENOMEM;
		goto error1;
	}
	dev->priv = priv->dev_data;
	return bathos_dev_open(pipe);

error1:
	bathos_free_buffer(priv->buffer_area, plat->nbufs * plat->bufsize);
error0:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}


const struct bathos_dev_ops esp8266_spim_dev_ops = {
	.open = esp8266_spim_open,
	.get_bqueue = bathos_dev_get_bqueue,
};
