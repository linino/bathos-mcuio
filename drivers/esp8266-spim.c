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
#include <bathos/irq.h>
#include <ets_sys.h>
#include <osapi.h>
#include <user_interface.h>
#include <mach/hw.h>

/*
 * Don't seem to work for master, as far as I can understand
 * Esp8266 documentation sucks !!!
 */
#define USE_INTERRUPTS 0

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
#  define TRANS_DONE    BIT(4)
#if USE_INTERRUPTS
#  define ALL_INTERRUPTS 0x3e0
#else
#  define ALL_INTERRUPTS 0
#endif /* USE_INTERRUPTS */

#define SPI_W(j)	(0x40 + (j))

struct esp8266_spim_priv {
	const struct esp8266_spim_platform_data *plat;
	struct list_head queue;
	void *buffer_area;
	struct bathos_bdescr *b;
	struct bathos_sglist_el *tx_el;
	struct bathos_sglist_el *rx_el;
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

static int _wait_for_trans_end(struct esp8266_spim_priv *priv)
{
	uint32_t v;

	while(1) {
		v = readl(SPI_SLAVE + priv->plat->base);
		if (v & TRANS_DONE)
			break;
	}
}

static void _transaction_done(struct esp8266_spim_priv *priv)
{
	unsigned int i;

	if (!priv->b)
		return;
	if (priv->rx_el) {
		uint8_t *ptr = priv->rx_el->data;

		for (i = 0; i < priv->rx_el->len; i++) {
			/* MISO data is in the second half of the buffer */
			uint32_t v = readl(SPI_W(i) + 32 + priv->plat->base);

			ptr[i++] = v & 0xff;
			if (i < priv->rx_el->len)
				ptr[i++] = (v >> 8) & 0xff;
			if (i < priv->rx_el->len)
				ptr[i++] = (v >> 16) & 0xff;
			if (i < priv->rx_el->len)
				ptr[i++] = (v >> 24) & 0xff;
		}
	}
	priv->b->error = 0;
	if (priv->plat->cs_deactivate)
		priv->plat->cs_deactivate(priv->plat->instance);
	bathos_bqueue_server_buf_processed(priv->b);
}

/*
 * Interrupt handler invoked in main context
 */
void esp8266_spim_int_handler(struct bathos_dev *dev)
{
	_transaction_done(dev->ll_priv);
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
		switch (e->dir) {
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

static int _do_check(struct bathos_bdescr *b,
		     void **data, unsigned int *len, enum buffer_dir dir)
{
	if (!b->data && list_empty(&b->sglist))
		return -1;
	if (!list_empty(&b->sglist)) {
		struct bathos_sglist_el *e;

		list_for_each_entry(e, &b->sglist, list) {
			if (e->dir == dir) {
				*data = e->data;
				*len = e->len;
				return 0;
			}
		}
	}
	if (b->dir == OUT) {
		*data = b->data;
		*len = b->data_size;
		return 0;
	}
	return -1;
}

static int _check_send(struct bathos_bdescr *b,
		       void **data, unsigned int *len)
{
	return _do_check(b, data, len, OUT);
}

static int _check_recv(struct bathos_bdescr *b, void *data,
		       unsigned int *len)
{
	return _do_check(b, data, len, IN);
}

static int _setup_tx(struct esp8266_spim_priv *priv, void *data,
		     unsigned int data_len)
{
	unsigned int i, j;
	uint8_t *ptr = data;
	uint32_t v;
	

	if (data_len > 32)
		printf("%s: WARNING: tx data truncated to 32 bytes\n",
		       __func__);
	v = readl(SPI_USER1 + priv->plat->base);
	v &= ~(MOSI_BITLEN_MASK << MOSI_BITLEN_SHIFT);
	v |= ((data_len << 3) & MOSI_BITLEN_MASK) << MOSI_BITLEN_SHIFT;
	writel(v, SPI_USER1 + priv->plat->base);

	/* Copy data to tx buffer area */
	for (i = 0; i < data_len; ) {
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
	uint32_t v = readl(SPI_USER1 + priv->plat->base);

	v &= ~(MISO_BITLEN_MASK << MISO_BITLEN_SHIFT);
	v |= ((data_len << 3) & MISO_BITLEN_MASK) << MISO_BITLEN_SHIFT;
	writel(v, SPI_USER1 + priv->plat->base);
	return 0;
}

static void start_trans(struct esp8266_spim_priv *priv)
{
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
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

	priv->tx_el = NULL;
	priv->rx_el = NULL;
	_disable_all(priv);
	/* Setup first */
	switch (op->type) {
	case NONE:
		/* THIS CAN'T BE, NONE ops are discarded by setup handler */
		printf("%s: NONE operation !\n", __func__);
		break;
	case BIDIR:
		if (_check_bidir(b, &priv->tx_el, &priv->rx_el) < 0) {
			b->error = -EINVAL;
			bathos_bqueue_server_buf_processed(b);
			return;
		}
		/* FALL THROUGH */
	case SEND:
		if (priv->tx_el) {
			/* BIDIR */
			data = priv->tx_el->data;
			len = priv->tx_el->len;
			pr_debug("%s, BIDIR-SEND: data = %p, len = %d\n",
				 __func__, data, len);
		} else {
			/* SEND */
			pr_debug("%s: SEND ONLY\n", __func__);
			if (_check_send(b, &data, &len) < 0) {
				b->error = -EINVAL;
				bathos_bqueue_server_buf_processed(b);
				return;
			}
		}
		_enable_mosi(priv);
		if (_setup_tx(priv, data, len) < 0) {
			b->error = -EINVAL;
			bathos_bqueue_server_buf_processed(b);
			return;
		}
		if (op->type == SEND)
			break;
		/* FALL THROUGH: RECV + SEND */
	case RECV:
		_enable_miso(priv);
		if (priv->rx_el) {
			/* BIDIR */
			pr_debug("%s RECV only\n", __func__);
			data = priv->rx_el->data;
			len = priv->rx_el->len;
		} else {
			/* RECV */
			pr_debug("%s RECV only\n", __func__);
			if (_check_recv(b, &data, &len) < 0) {
				b->error = -EINVAL;
				bathos_bqueue_server_buf_processed(b);
				return;
			}
		}
		if (_setup_rx(priv, data, len) < 0) {
			b->error = -EINVAL;
			bathos_bqueue_server_buf_processed(b);
			return;
		}
		break;
	}
	/* And then finally start transaction */
	priv->b = b;
	/* Activate cs */
	if (priv->plat->cs_activate)
		priv->plat->cs_activate(priv->plat->instance);
	_start(priv);
	if (!(USE_INTERRUPTS)) {
		if (_wait_for_trans_end(priv) < 0)
			return;
		_transaction_done(priv);
	}
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
	/* No direct sysclk to spiclk */
	v = readl(PERIPHS_IO_MUX);
	v &= ~(1 << 9);
	writel(v, PERIPHS_IO_MUX);

	if (plat->setup_pins)
		return plat->setup_pins(plat->instance);

	if (USE_INTERRUPTS) {
		int irq;

		switch (plat->instance) {
		case 0:
			irq = SPI0_IRQN;
			break;
		case 1:
			irq = SPI1_IRQN;
			break;
		case 2:
			irq = SPI2_IRQN;
			break;
		default:
			irq = -1;
			printf("%s: invalid instance number\n", __func__);
			break;
		}
		if (irq > 0)
			bathos_enable_irq(irq);
	}

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
		b->error = -ENOSYS;
		bathos_bqueue_server_buf_processed(b);
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

static int esp8266_spim_close(struct bathos_pipe *pipe)
{
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(pipe);
	struct esp8266_spim_priv *priv = bathos_bqueue_to_ll_priv(bq);
	struct bathos_dev *dev = pipe->dev;
	const struct esp8266_spim_platform_data *plat = dev->platform_data;
	struct bathos_bdescr *b, *tmp;

	/* Tell the client we're closing */
	list_for_each_entry_safe(b, tmp, &priv->queue, list) {
		/* buffer queue is being killed */
		b->error = -EPIPE;
		bathos_bqueue_server_buf_processed_immediate(b);
	}
	/* Stop and finalize the queue */
	bathos_bqueue_stop(bq);
	bathos_bqueue_server_fini(bq);
	/* Free the remaining resources */
	bathos_dev_close(pipe);
	bathos_dev_uninit(pipe);
	bathos_free_buffer(priv->buffer_area, plat->nbufs * plat->bufsize);
	bathos_free_buffer(priv, sizeof(*priv));
	return 0;
}

const struct bathos_dev_ops esp8266_spim_dev_ops = {
	.open = esp8266_spim_open,
	.get_bqueue = bathos_dev_get_bqueue,
	.close = esp8266_spim_close,
};
