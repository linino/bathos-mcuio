/*
 * Use the bus pirate as an spi master under bathos (arch-unix).
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/bp-spim.h>
#include <bathos/cmdline.h>
#include <bathos/buffer_queue_server.h>

#define MAX_BP_SPIM_INSTANCES 3

struct bp_spim_priv {
	int fd;
	const struct bp_spim_platform_data *plat;
	struct list_head queue;
	void *buffer_area;
	struct bathos_bdescr *b;
	struct bathos_sglist_el *tx_el;
	struct bathos_sglist_el *rx_el;
};

declare_event(bp_spim_setup);
declare_event(bp_spim_done);

static int bp_spim_fds[MAX_BP_SPIM_INSTANCES] = {
	[ 0 ... MAX_BP_SPIM_INSTANCES - 1] = -1,
};

static int get_reply(struct bp_spim_priv *priv, uint8_t *buf, int len,
		     unsigned long timeout_ms)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = priv->fd;
	pfd.events = POLLIN;
	switch (poll(&pfd, 1, timeout_ms)) {
	case 0:
		pr_debug("%s: timeout\n", __func__);
		return 0;
	case -1:
		printf("%s: poll(): %s\n", __func__, strerror(errno));
		return -1;
	default:
		ret = read(priv->fd, buf, len);
		if (ret < 0) {
			printf("%s: read(): %s\n", __func__, strerror(errno));
			return -1;
		}
		return ret;
	}
	return -1;
}

static int do_cs(struct bp_spim_priv *priv, int assert)
{
	uint8_t cmd, reply;
	const struct bp_spim_platform_data *plat = priv->plat;

	cmd = assert ? (plat->cs_active_state ? 3 : 2) :
		(plat->cs_active_state ? 2 : 3);
	if (write(priv->fd, &cmd, 1) < 0) {
		printf("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(priv, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		printf("%s: error setting spi power and cs\n", __func__);
		return -1;
	}
	return 0;
}

static int cs_assert(struct bp_spim_priv *priv)
{
	return do_cs(priv, 1);
}

static int cs_deassert(struct bp_spim_priv *priv)
{
	return do_cs(priv, 0);
}

static int do_write(int fd, const char *buf, unsigned long sz)
{
	int stat, sent;

	for (sent = 0; sent < sz; sent += stat) {
		/*
		 * WORK AROUND APPARENT BUS PIRATE OVERRUN FOR LENGTH >= 8:
		 * WRITE 1 BYTE A TIME !
		 */
		stat = write(fd, &buf[sent], 1);
		usleep(200);
		if (stat < 0) {
			printf("%s, write: %s\n", __func__, strerror(errno));
			return stat;
		}
	}
	return sent;
}

static int do_read(int fd, char *buf, unsigned long sz)
{
	int stat, done;
	struct pollfd pfd;

	for (done = 0; done < sz; done += stat) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		switch (poll(&pfd, 1, 100)) {
		case -1:
			printf("%s, poll: %s\n", __func__, strerror(errno));
			return -1;
		case 0:
			return 0;
		default:
			stat = read(fd, &buf[done], sz - done);
			if (stat < 0) {
				printf("%s, read: %s\n",
					__func__, strerror(errno));
				return stat;
			}
			break;
		}
	}
	return done;
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
	pr_debug("%s OK: rx_el = %p, tx_el = %p\n", __func__, *rx_el, *tx_el);
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
	if (b->dir == dir) {
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

static int _bulk_xfer(struct bp_spim_priv *priv,
		      const char *out_buf, char *in_buf,
		      unsigned long size)
{
	char my_buf[17], *ptr, dummy_reply[16];
	unsigned short sz = 0;
	uint8_t reply;
	unsigned long done;
	static const char dummy_outbuf[17] = { [0 ... 16 ] = 0xbb, };

	pr_debug("%s entered, size = %lu\n", __func__, size);
	cs_assert(priv);
	for (done = 0; done < size; done += sz) {
		sz = min(size - done, 16);

		if (sz < 1)
			return sz;

		/* Command: write and read (bulk xfer) */
		my_buf[0] = 0x10 | (sz - 1);
		pr_debug("sz = %u, my_buf[0] = 0x%02x\n",
			 sz, (unsigned int)my_buf[0]);
		memcpy(&my_buf[1],
		       out_buf ? &out_buf[done] : &dummy_outbuf[done], sz);

		/* Send write data (0xff) */
		if (do_write(priv->fd, my_buf, sz + 1) < 0) {
			cs_deassert(priv);
			return -1;
		}
		/* Wait for ack */
		if (get_reply(priv, &reply, 1, 1000) <= 0) {
			printf("%s: timeout/error from bus pirate\n", __func__);
			cs_deassert(priv);
			return -1;
		}
		if (reply != 1) {
			printf("%s: unexpected reply 0x%02x\n", __func__,
			       (unsigned int)reply);
			cs_deassert(priv);
			return -1;
		}
		/* Get read data */
		ptr = in_buf ? &in_buf[done] : dummy_reply;
		if (do_read(priv->fd, ptr, sz) < 0) {
			cs_deassert(priv);
			return -1;
		}
	}
	cs_deassert(priv);
	pr_debug("returning from %s, size = %lu\n", __func__, size);
	return size;
}

static int bp_spim_config(struct bp_spim_priv *priv);

static void start_trans(struct bp_spim_priv *priv)
{
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	void *rx_data, *tx_data;
	unsigned int rx_len, tx_len;
	int stat;

	if (list_empty(&priv->queue)) {
		printf("%s invoked with empty transaction queue\n", __func__);
		return;
	}
	b = list_first_entry(&priv->queue, struct bathos_bdescr, list);
	op = to_operation(b);
	if (list_empty(&b->sglist)) {
		printf("%s: only sglist supported\n", __func__);
		return;
	}

	priv->tx_el = NULL;
	priv->rx_el = NULL;
	rx_data = tx_data = NULL;
	rx_len = tx_len = 0;
	/* Setup first */
	switch (op->type) {
	case NONE:
		/* THIS CAN'T BE, NONE ops are discarded by setup handler */
		printf("%s: NONE operation !\n", __func__);
		break;
	case BIDIR:
		pr_debug("BIDIR OP\n");
		if (_check_bidir(b, &priv->tx_el, &priv->rx_el) < 0) {
			b->error = -EINVAL;
			return;
		}
		/* FALL THROUGH */
	case SEND:
		if (priv->tx_el) {
			/* BIDIR */
			tx_data = priv->tx_el->data;
			tx_len = priv->tx_el->len;
			pr_debug("%s, BIDIR-SEND: tx_data = %p, tx_len = %d\n",
				 __func__, tx_data, tx_len);
		} else {
			/* SEND */
			pr_debug("%s: SEND ONLY\n", __func__);
			if (_check_send(b, &tx_data, &tx_len) < 0) {
				b->error = -EINVAL;
				return;
			}
		}
		if (op->type == SEND)
			break;
		/* FALL THROUGH: RECV + SEND */
	case RECV:
		if (priv->rx_el) {
			/* BIDIR */
			rx_data = priv->rx_el->data;
			rx_len = priv->rx_el->len;
			pr_debug("%s BIDIR-RECV: \n", __func__);
			if (rx_len != tx_len) {
				b->error = -EINVAL;
				return;
			}
		} else {
			/* RECV */
			pr_debug("%s RECV only\n", __func__);
			if (_check_recv(b, &rx_data, &rx_len) < 0) {
				b->error = -EINVAL;
				return;
			}
		}
		break;
	}
	/* And then finally start transaction */
	priv->b = b;
	bp_spim_config(priv);
	stat = _bulk_xfer(priv, tx_data, rx_data, tx_data ? tx_len : rx_len);
	b->error = stat < 0 ? stat : 0;
}


/*
 * This is invoked when a buffer has been setup by the client and is
 * ready for tx/rx submission
 */
static void bp_spim_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;
	struct bathos_bqueue *q;
	struct bp_spim_priv *priv;

	pr_debug("%s (buffer %p)\n", __func__, b);
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
		bathos_bqueue_server_buf_processed(b);
		break;
	default:
		printf("%s: ERR: invalid operation type\n", __func__);
		b->error = -ENOSYS;
		bathos_bqueue_server_buf_processed(b);
		break;
	}
}
declare_event_handler(bp_spim_setup, NULL, bp_spim_setup_handler,
		      NULL);

/*
 * This is invoked when a buffer has been released by the client
 */
static void bp_spim_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;

	pr_debug("%s (buffer %p)\n", __func__, b);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(bp_spim_done, NULL, bp_spim_done_handler,
		      NULL);

static int flush_input(int fd)
{
	char tmp_buf[20];

	while(do_read(fd, tmp_buf, sizeof(tmp_buf)) > 0);
	return 0;
}

static int bp_spim_config(struct bp_spim_priv *priv)
{
	uint8_t cmd;
	uint8_t reply;

	/* Set speed = 125KHz */
	cmd = 0x60;
	if (write(priv->fd, &cmd, 1) < 0) {
		printf("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(priv, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		printf("%s: error setting spi frequency\n", __func__);
		return -1;
	}
	/* 3.3V on, cpol = 0, cphase = 1 */
	cmd = 0x8a;
	if (write(priv->fd, &cmd, 1) < 0) {
		printf("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(priv, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		printf("%s: error setting spi frequency\n", __func__);
		return -1;
	}
	/* Set power and cs */
	cmd = 0x4c;
	if (write(priv->fd, &cmd, 1) < 0) {
		printf("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(priv, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		printf("%s: error setting spi power and cs\n", __func__);
		return -1;
	}
	return 0;
}

static int bp_spim_async_open(void *_priv)
{
	struct bp_spim_priv *priv = _priv;
	const struct bp_spim_platform_data *plat = priv->plat;
	char reply[8];
	int i, ret;

	if (bp_spim_fds[plat->instance] == -1) {
		printf("%s: invalid fd for instance\n", __func__);
		return -ENODEV;
	}
	priv->fd = bp_spim_fds[plat->instance];
	for (i = 0; i < 20; i++) {
		if (do_write(priv->fd, "\0", 1) < 1)
			return -1;
		if (do_read(priv->fd, reply, 5) == 5)
			break;
	}
	if (memcmp(reply, "BBIO", 4)) {
		printf("%s: invalid reply %s\n", __func__, reply);
		return -EINVAL;
	}
	printf("BUS PIRATE DETECTED raw bitbang version %c\n", reply[4]);
	flush_input(priv->fd);
	/* Now enter binary spi mode */
	if (write(priv->fd, "\1", 1) < 0) {
		printf("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (do_read(priv->fd, reply, 4) < 0) {
		printf("%s: read(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (memcmp(reply, "SPI", 3)) {
		printf("%s: invalid reply %s\n", __func__, reply);
		return ret;
	}
	printf("SPI protocol version %c\n", reply[3]);
	return bp_spim_config(priv);
}

/* bp_spim_fd=%d */
int parse_bp_spim(const char *n, void *dummy)
{
	int i = atoi(&n[strlen("bp_spim_fd")]);

	if (i >= MAX_BP_SPIM_INSTANCES) {
		printf("%s: invalid spim instance %d\n", __func__, i);
		return -EINVAL;
	}
	bp_spim_fds[i] = atoi(&n[strlen("bp_spim_fdX=")]);
	return 0;
}

declare_cmdline_handler(bp_spim_fd, parse_bp_spim, NULL);

static const struct bathos_ll_dev_ops bp_spim_ll_dev_ops = {
	.async_open = bp_spim_async_open,
};


static int bp_spim_open(struct bathos_pipe *pipe)
{
	struct bp_spim_priv *priv;
	struct bathos_dev *dev = pipe->dev;
	const struct bp_spim_platform_data *plat = dev->platform_data;
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
	pipe->dev_data = bathos_dev_init(dev, &bp_spim_ll_dev_ops,
					 priv);
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
						&event_name(bp_spim_setup),
						&event_name(bp_spim_done),
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

static int bp_spim_close(struct bathos_pipe *pipe)
{
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(pipe);
	struct bp_spim_priv *priv = bathos_bqueue_to_ll_priv(bq);
	struct bathos_dev *dev = pipe->dev;
	const struct bp_spim_platform_data *plat = dev->platform_data;
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


const struct bathos_dev_ops bp_spim_dev_ops = {
	.open = bp_spim_open,
	.get_bqueue = bathos_dev_get_bqueue,
	.close = bp_spim_close,
};
