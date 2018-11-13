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
	const struct esp8266_spim_platform_data *plat;
	struct list_head queue;
	void *buffer_area;
};

declare_event(esp8266_spim_setup);
declare_event(esp8266_spim_done);

static inline void _start(struct esp8266_spim_priv *priv)
{
	uint32_t v = readl(priv->plat->base + SPI_CMD);

	writel(v | SPI_BUSY, priv->plat->base + SPI_CMD);
}

static inline void _do_mosi(struct esp8266_spim_priv *priv, int enable)
{
	uint32_t v = readl(priv->plat->base + SPI_USER);

	if (enable)
		v |= MOSI_EN;
	else
		v &= ~MOSI_EN;
	writel(v, priv->plat->base + SPI_USER);
}

static inline void _enable_mosi(struct esp8266_spim_priv *priv)
{
	_do_mosi(priv, 1);
}

static inline void _disable_mosi(struct esp8266_spim_priv *priv)
{
	_do_mosi(priv, 0);
}

static inline void _do_miso(struct esp8266_spim_priv *priv, int enable)
{
	uint32_t v = readl(priv->plat->base + SPI_USER);

	if (enable)
		v |= MISO_EN;
	else
		v &= ~MISO_EN;
	writel(v, priv->plat->base + SPI_USER);
}

static inline void _enable_miso(struct esp8266_spim_priv *priv)
{
	_do_miso(priv, 1);
}

static inline void _disable_miso(struct esp8266_spim_priv *priv)
{
	_do_miso(priv, 0);
}

static inline void _disable_all(struct esp8266_spim_priv *priv)
{
	uint32_t v = readl(priv->plat->base + SPI_USER);

	v &= ~(COMMAND_EN | ADDR_EN | DUMMY_EN | MISO_EN | MOSI_EN);
	writel(v, priv->plat->base + SPI_USER);
}

static int _check_bidir(struct bathos_bdescr *b,
			struct bathos_sglist_el **tx_el,
			struct bathos_sglist_el**rx_el)
{
	struct bathos_sglist_el *e;
	int rx_found = 0, tx_found = 0;
	*tx_el = NULL; *rx_el = NULL;

	if (list_empty(&b->sglist)) {
		/* Must have an sglist ! */
		printf("%s: sglist empty\n", __func__);
		return -1;
	}
	list_for_each_entry(e, &b->sglist, list) {
		switch (b->dir) {
		case ANY:
			printf("%s: invalid ANY element\n", __func__);
			return -1;
		case OUT:
			tx_found++;
			*tx_el = e;
			break;
		case IN:
			rx_found++;
			*rx_el = e;
			break;
		default:
			printf("%s: invalid %d element\n", __func__, b->dir);
			return -1;
		}
	}
	if ((rx_found != 1) || (tx_found != 1)) {
		printf("%s: unsupported rx = %d, tx = %d\n", __func__,
		       rx_found, tx_found);
		return -1;
	}
	return 0;
}

static int _check_send(struct bathos_bdescr *b,
		       void **data, unsigned int *len)
{
	if (!b->data && list_empty(&b->sglist))
		return -1;
	if (!list_empty(&b->sglist)) {
		struct bathos_sglist_el *e;

		e = list_first_entry(&b->sglist,
				     struct bathos_sglist_el, list);
		*data = e->data;
		*len = e->len;
		return 0;
	}
	*data = b->data;
	*len = b->data_size;
	return 0;
}

static int _check_recv(struct bathos_bdescr *b, void *data,
		       unsigned int *len)
{
	/* Same checks */
	return _check_send(b, data, len);
}

static int _setup_tx(struct esp8266_spim_priv *priv, void *data,
		     unsigned int data_len)
{
	unsigned int i, j;
	uint8_t *ptr = data;

	if (data_len > 32)
		printf("%s: WARNING: tx data truncated to 32 bytes\n",
		       __func__);

	/* Copy data to tx buffer area */
	for (i = 0; i < data_len; ) {
		uint32_t v;

		j = i;
		v = ptr[i++];
		if (i < data_len)
			v |= ptr[i++] << 8;
		if (i < data_len)
			v |= ptr[i++] << 16;
		if (i < data_len)
			v |= ptr[i++] << 24;
		writel(v, SPI_W(j) + priv->plat->base);
	}
	return 0;
}

static int _setup_rx(struct esp8266_spim_priv *priv, void *data,
		     unsigned int data_len)
{
	/* Nothing to do for the moment */
	return 0;
}

static void start_trans(struct esp8266_spim_priv *priv)
{
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	struct bathos_sglist_el *tx_el = NULL, *rx_el = NULL;
	void *data;
	unsigned int len;

	if (list_empty(&priv->queue)) {
		printf("%s invoked with empty transaction queue\n", __func__);
		return;
	}
	b = list_first_entry(&priv->queue, struct bathos_bdescr, list);
	op = to_operation(b);
	if (b->data) {
		data = b->data;
		len = b->data_size;
	}

	_disable_all(priv);
	/* Setup first */
	switch (op->type) {
	case NONE:
		/* THIS CAN'T BE, NONE ops are discarded by setup handler */
		printf("%s: NONE operation !\n", __func__);
		break;
	case BIDIR:
		if (_check_bidir(b, &tx_el, &rx_el) < 0) {
			bathos_bqueue_server_buf_done(b);
			return;
		}
		/* FALL THROUGH */
	case SEND:
		if (tx_el) {
			/* BIDIR */
			data = tx_el->data;
			len = tx_el->len;
		} else {
			/* SEND */
			if (_check_send(b, &data, &len) < 0) {
				bathos_bqueue_server_buf_done(b);
				return;
			}
		}
		_enable_mosi(priv);
		if (_setup_tx(priv, data, len) < 0) {
			bathos_bqueue_server_buf_done(b);
			return;
		}
		if (op->type == SEND)
			break;
		/* FALL THROUGH: RECV + SEND */
	case RECV:
		_enable_miso(priv);
		if (rx_el) {
			/* BIDIR */
			data = rx_el->data;
			len = rx_el->len;
		} else {
			/* RECV */
			if (_check_recv(b, &data, &len) < 0) {
				bathos_bqueue_server_buf_done(b);
				return;
			}
		}
		if (_setup_rx(priv, data, len) < 0) {
			bathos_bqueue_server_buf_done(b);
			return;
		}
		break;
	}
	/* And then finally start transaction */
	_start(priv);
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
	if (op->addr.type != SPIM) {
		printf("%s: UNSUPPORTED OP TYPE %d\n", __func__, op->addr.type);
		return;
	}
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
	case BIDIR:
	case RECV:
		/*
		 * Move buffer to tail of transaction queue and start a
		 * transmission
		 */
		list_move_tail(&b->list, &priv->queue);
		start_trans(priv);
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
	struct esp8266_spim_priv *priv = _priv;

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
	INIT_LIST_HEAD(&priv->queue);
	INIT_LIST_HEAD(&priv->queue);
	priv->buffer_area = bathos_alloc_buffer(plat->nbufs * plat->bufsize);
	if (!priv->buffer_area) {
		ret = -ENOMEM;
		goto error0;
	}
	pipe->dev_data = bathos_dev_init(dev, &esp8266_spim_ll_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENOMEM;
		goto error1;
	}
	ret = bathos_dev_open(pipe);
	if (ret < 0)
		goto error2;
	if (pipe->mode & BATHOS_MODE_ASYNC) {
		struct bathos_bqueue *q = dev->ops->get_bqueue ?
			dev->ops->get_bqueue(pipe) : NULL;

		if (!q)
			goto error2;
		ret = bathos_bqueue_server_init(q,
						&event_name(esp8266_spim_setup),
						&event_name(esp8266_spim_done),
						priv->buffer_area,
						plat->nbufs,
						plat->bufsize,
						SPIM);
	}
	if (ret)
		goto error2;
	return ret;

error2:
	bathos_dev_uninit(pipe);
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
