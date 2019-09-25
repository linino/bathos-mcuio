/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* gpio bitbang spi master driver */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/delay.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/bb-spim.h>
#include <bathos/buffer_queue_server.h>
#include <bathos/io.h>
#include <bathos/gpio.h>
#include <bathos/irq.h>
#include <mach/hw.h>

#define CS_ACTIVE	0
#define CS_IDLE		1
#define CLK_IDLE	0
#define CLK_ACTIVE	1
#define MOSI_IDLE	0
#define MOSI_ACTIVE	1
#define MISO_IDLE	0
#define MISO_ACTIVE	1

#ifdef CONFIG_BB_SPIM_DETECT_NOT_READY

#if CONFIG_BB_SPIM_NOT_READY_CHAR == 0
# error "Not ready char MUST NOT BE 0"
#endif

#define NOT_READY (((uint32_t)CONFIG_BB_SPIM_NOT_READY_CHAR) |	\
		   (((uint32_t)CONFIG_BB_SPIM_NOT_READY_CHAR) << 8) | \
		   (((uint32_t)CONFIG_BB_SPIM_NOT_READY_CHAR) << 16) | \
		   (((uint32_t)CONFIG_BB_SPIM_NOT_READY_CHAR) << 24))
#else
#define NOT_READY 0
#endif

#ifdef HAS_ARCH_NDELAY
static inline int _ndelay(int d)
{
	return __arch_ndelay(d);
}
#else
static inline int _ndelay(int d)
{
	return udelay(d / 1000);
}
#endif

struct bb_spim_priv {
	const struct bb_spim_platform_data *plat;
	struct list_head queue;
	void *buffer_area;
	struct bathos_bdescr *b;
	struct bathos_sglist_el *tx_el;
	struct bathos_sglist_el *rx_el;
	uint32_t service_buf[8];
	int send_only;
	unsigned int clk_half_period_ns;
};

declare_event(bb_spim_setup);
declare_event(bb_spim_done);

static void _transaction_done(struct bb_spim_priv *priv, char *rx_data,
			      int rx_len, const uint8_t *eb)
{
	int i;
	if (!priv->b)
		return;

	pr_debug("%s: rx_data = %p, rx_len = %d\n", __func__, rx_data, rx_len);
	for (i = 0; NOT_READY && rx_data && i < rx_len; i++) {
		pr_debug("rx_data[%d] = 0x%02x\n", i, rx_data[i]);
		if (rx_data[i] != CONFIG_BB_SPIM_NOT_READY_CHAR) {
			pr_debug("READY\n");
			break;
		}
	}
	if (i >= rx_len) {
		if (eb) {
			pr_debug("*eb = 0x%02x\n", *eb);
			if (*eb == CONFIG_BB_SPIM_NOT_READY_CHAR) {
				pr_debug("NOT READY !\n");
				priv->b->error = -EAGAIN;
			}
		}
	}

	bathos_bqueue_server_buf_processed(priv->b);
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
		     struct bathos_sglist_el **el, enum buffer_dir dir)
{
	struct bathos_sglist_el *e;
	if (list_empty(&b->sglist))
		return -1;

	list_for_each_entry(e, &b->sglist, list) {
		if (e->dir == dir) {
			*el = e;
			return 0;
		}
	}
	return -1;
}

static int _check_send(struct bathos_bdescr *b,
		       struct bathos_sglist_el **tx_el)
{
	return _do_check(b, tx_el, OUT);
}

static int _check_recv(struct bathos_bdescr *b,
		       struct bathos_sglist_el **rx_el)
{
	return _do_check(b, rx_el, IN);
}

#if defined CONFIG_ARCH_XTENSA && MACH==eagle
/* Temporary: optimized 3MHz implementation for esp8266 */
static void _do_byte(struct bb_spim_priv *priv, const char *tx, char *rx)
{
	const struct bb_spim_platform_data *plat = priv->plat;
	uint32_t mask, in, out;
	int j;
	uint32_t clk_mask = BIT(plat->clk_gpio);
	uint32_t miso_mask = BIT(plat->miso_gpio);
	uint32_t mosi_mask = BIT(plat->mosi_gpio);

	in = 0;
	out = tx ? *tx : 0;
	for (j = 0, mask = 0x80; j < 8; j++, mask >>= 1) {
		if (out & mask)
			writel(mosi_mask | clk_mask, GPIO_OUT_SET);
		else {
			writel(mosi_mask, GPIO_OUT_CLEAR);
			writel(clk_mask, GPIO_OUT_SET);
		}
		if (readl(GPIO_IN) & miso_mask)
			in |= mask;
		writel(clk_mask, GPIO_OUT_CLEAR);
	}
	if (rx)
		*rx = in;
}
#else
static void _do_byte(struct bb_spim_priv *priv, const char *tx, char *rx)
{
	const struct bb_spim_platform_data *plat = priv->plat;
	uint32_t mask, in, out;
	int j;

	in = 0;
	out = tx ? *tx : 0;
	for (j = 0, mask = 0x80; j < 8; j++, mask >>= 1) {
		if (tx)
			gpio_set(plat->mosi_gpio, (out & mask) ?
				 MOSI_ACTIVE : MOSI_IDLE);
		_ndelay(priv->clk_half_period_ns);
		gpio_set(plat->clk_gpio, CLK_ACTIVE);
		if (!!gpio_get(plat->miso_gpio) == MISO_ACTIVE)
			in |= mask;
		_ndelay(priv->clk_half_period_ns);
		gpio_set(plat->clk_gpio, CLK_IDLE);
	}
	if (rx)
		*rx = in;
}
#endif

static void _do_transaction(struct bb_spim_priv *priv, const char *tx_data,
			    unsigned int tx_len,
			    char *rx_data, unsigned int rx_len,
			    char *extra_rx)
{
	const struct bb_spim_platform_data *plat = priv->plat;
	unsigned int l = tx_len > rx_len ? tx_len : rx_len;
	unsigned int i;
	int do_tx, do_rx;

	pr_debug("clk idle\n");
	gpio_set(plat->clk_gpio, CLK_IDLE);
	udelay(1);
	pr_debug("cs active\n");
	gpio_set(plat->cs_gpio, CS_ACTIVE);
	for (i = 0; i < l; i++) {
		do_tx = tx_data && i < tx_len;
		do_rx = rx_data && i < rx_len;
		/* MSB FIRST */
		_do_byte(priv, do_tx ? &tx_data[i] : NULL,
			 do_rx ? &rx_data[i] : NULL);
	}
	if (extra_rx)
		_do_byte(priv, NULL, extra_rx);
	gpio_set(plat->cs_gpio, CS_IDLE);
	if (tx_data) {
		pr_debug("Sent:\n");
		for (i = 0; i < tx_len; i++) {
			pr_debug("0x%02x (%c)\t", tx_data[i],
				 tx_data[i] > 0 && tx_data[i] < 128 ?
				 tx_data[i] : '.');
			if (i && !((i + 1) % 4))
				pr_debug("\n");
		}
		pr_debug("\n");
	}
	if (rx_data) {
		pr_debug("Received:\n");
		for (i = 0; i < rx_len; i++) {
			pr_debug("0x%02x (%c)\t", rx_data[i],
				 rx_data[i] > 0 && rx_data[i] < 128 ?
				 rx_data[i] : '.');
			if (i && !((i + 1) % 4))
				pr_debug("\n");
		}
		pr_debug("\n");
	}
}

static void start_trans(struct bb_spim_priv *priv)
{
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	void *tx_data, *rx_data;
	unsigned int tx_len, rx_len;
	uint8_t extra_byte, *eb = NULL;
	
	if (list_empty(&priv->queue)) {
		printf("%s invoked with empty transaction queue\n", __func__);
		return;
	}
	b = list_first_entry(&priv->queue, struct bathos_bdescr, list);
	op = to_operation(b);
	priv->tx_el = NULL;
	priv->rx_el = NULL;
	tx_data = rx_data = NULL;
	tx_len = rx_len = 0;
	priv->send_only = 0;
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
		if (!priv->tx_el) {
			/* SEND */
			pr_debug("%s: SEND ONLY\n", __func__);
			if (_check_send(b, &priv->tx_el) < 0) {
				b->error = -EINVAL;
				printf("%s: Tx only and no tx element\n",
				       __func__);
				bathos_bqueue_server_buf_processed(b);
				return;
			}
		} else
			pr_debug("%s: BIDIR SEND\n", __func__);
		tx_data = priv->tx_el->data;
		tx_len = priv->tx_el->len;
		pr_debug("%s: tx_el = %p\n", __func__, priv->tx_el);
		if (op->type == SEND) {
			if (NOT_READY) {
				/*
				 * Send only. Also enable miso. We have to check
				 * whether the other end is sending the
				 * default char
				 */
				rx_data = priv->service_buf;
				rx_len = sizeof(priv->service_buf);
				priv->send_only = 1;
			}
			break;
		}
		/* FALL THROUGH: RECV + SEND */
	case RECV:
		if (!priv->rx_el) {
			pr_debug("%s RECV only\n", __func__);
			if (_check_recv(b, &priv->rx_el) < 0) {
				b->error = -EINVAL;
				bathos_bqueue_server_buf_processed(b);
				printf("%s: Rx only and no rx element\n",
				       __func__);
				return;
			}
		} else
			/* BIDIR */
			pr_debug("%s BIDIR-RECV\n", __func__);
		rx_data = priv->rx_el->data;
		rx_len = priv->rx_el->len;
		pr_debug("%s: rx_el = %p\n", __func__, priv->rx_el);
		break;
	}
	/* And then finally start transaction */
	priv->b = b;
	if (NOT_READY)
		eb = &extra_byte;
	_do_transaction(priv, tx_data, tx_len, rx_data, rx_len, eb);
	_transaction_done(priv, rx_data, rx_len, eb);
}

static int _spim_init(struct bb_spim_priv *priv)
{
	const struct bb_spim_platform_data *plat = priv->plat;
	int stat;

	if (!plat) {
		printf("%s: missing platform data\n", __func__);
		return -EINVAL;
	}
	/* Fixed mode 0 for the moment, cs active low */
	pr_debug("MISO: gpio %d, af = %d\n", plat->miso_gpio,
		 gpio_to_af(plat->miso_gpio));
	stat = gpio_dir_af(plat->miso_gpio, 0, 0, gpio_to_af(plat->miso_gpio));
	if (stat < 0) {
		printf("%s: cannot setup miso gpio\n", __func__);
		return stat;
	}
	pr_debug("MOSI: gpio %d, af = %d\n", plat->mosi_gpio,
		 gpio_to_af(plat->mosi_gpio));
	stat = gpio_dir_af(plat->mosi_gpio, 1, 0, gpio_to_af(plat->mosi_gpio));
	if (stat < 0) {
		printf("%s: cannot setup mosi gpio\n", __func__);
		return stat;
	}
	pr_debug("SCLK: gpio %d, af = %d\n", plat->clk_gpio,
		 gpio_to_af(plat->clk_gpio));
	stat = gpio_dir_af(plat->clk_gpio, 1, 0, gpio_to_af(plat->clk_gpio));
	if (stat < 0) {
		printf("%s: cannot setup clk gpio\n", __func__);
		return stat;
	}
	pr_debug("CS: gpio %d, af = %d\n", plat->cs_gpio,
		 gpio_to_af(plat->cs_gpio));
	stat = gpio_dir_af(plat->cs_gpio, 1, 1, gpio_to_af(plat->cs_gpio));
	if (stat < 0) {
		printf("%s: cannot setup clk gpio\n", __func__);
		return stat;
	}	
	priv->clk_half_period_ns = (1000000000UL / 2) / (plat->clk_freq * 1000);
#ifndef HAS_ARCH_NDELAY
	if (priv->clk_half_period_ns < 1000) {
		printf("%s: clock frequency is too high (max 500KHz)\n",
		       __func__);
		return -EINVAL;
	}
#endif
	pr_debug("half period ns = %u\n", priv->clk_half_period_ns);
	return 0;
}

/*
 * This is invoked when a buffer has been setup by the client and is
 * ready for tx/rx submission
 */
static void bb_spim_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;
	struct bathos_bqueue *q;
	struct bb_spim_priv *priv;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	b->error = 0;
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
declare_event_handler(bb_spim_setup, NULL, bb_spim_setup_handler, NULL);


/*
 * This is invoked when a buffer has been released by the client
 */
static void bb_spim_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(bb_spim_done, NULL, bb_spim_done_handler, NULL);

static int bb_spim_async_open(void *_priv)
{
	struct bb_spim_priv *priv = _priv;

	return _spim_init(priv);
}

static const struct bathos_ll_dev_ops bb_spim_ll_dev_ops = {
	.async_open = bb_spim_async_open,
};


static int bb_spim_open(struct bathos_pipe *pipe)
{
	struct bb_spim_priv *priv;
	struct bathos_dev *dev = pipe->dev;
	const struct bb_spim_platform_data *plat = dev->platform_data;
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
	pipe->dev_data = bathos_dev_init(dev, &bb_spim_ll_dev_ops, priv);
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
						&event_name(bb_spim_setup),
						&event_name(bb_spim_done),
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

static int bb_spim_close(struct bathos_pipe *pipe)
{
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(pipe);
	struct bb_spim_priv *priv = bathos_bqueue_to_ll_priv(bq);
	struct bathos_dev *dev = pipe->dev;
	const struct bb_spim_platform_data *plat = dev->platform_data;
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

const struct bathos_dev_ops bb_spim_dev_ops = {
	.open = bb_spim_open,
	.get_bqueue = bathos_dev_get_bqueue,
	.close = bb_spim_close,
};
