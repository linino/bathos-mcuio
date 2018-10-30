/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* lininoio core file */

#include <lininoio.h>

#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/string.h>
#include <bathos/errno.h>
#include <bathos/lininoio-internal.h>

declare_event(lininoio_buf_available);
declare_event(lininoio_buf_processed);

enum lininoio_state {
	UNASSOCIATED = 1,
	ASSOCIATING = 2,
	ASSOCIATED = 3,
};

struct lininoio_data {
	struct bathos_pipe *io_pipe;
	struct bathos_buffer_op_address host_address;
	enum lininoio_state state;
	/*
	 * Incremented each time a packet is sent, reset each time an alive
	 * message is sent
	 */
	int activity;
	int outstanding_rx_buffers;
};

static struct lininoio_data global_data;

#ifdef ARCH_IS_HARVARD

static inline struct lininoio_channel *
__get_channel(const struct lininoio_channel * PROGMEM c,
	      struct lininoio_channel *chan)
{
	memcpy_P(chan, c, sizeof(*chan));
	return chan;
}

static inline struct lininoio_channel_ops *
__get_channel_ops(const struct lininoio_channel *c,
		  struct lininoio_channel_ops *o)
{
	memcpy_P(o, c->ops, sizeof(*c->ops));
	return o;
}

static inline const struct lininoio_channel_descr *
__get_channel_descr(const struct lininoio_channel *c,
		    struct lininoio_channel_descr *cd)
{
	memcpy_P(cd, c->descr, sizeof(*c->descr));
	return cd;
}

#else /* !ARCH_IS_HARVARD */

static inline const struct lininoio_channel *
__get_channel(const struct lininoio_channel * PROGMEM c,
	      struct lininoio_channel *chan)
{
	return c;
}

static inline const struct lininoio_channel_ops *
__get_channel_ops(const struct lininoio_channel *c,
		  struct lininoio_channel_ops *o)
{
	return c->ops;
}

static inline const struct lininoio_channel_descr *
__get_channel_descr(const struct lininoio_channel *c,
		    struct lininoio_channel_descr *cd)
{
	return c->descr;
}

#endif /* !ARCH_IS_HARVARD */

/*
 * Task init function: open IO pipe and start it
 */
static int lininoio_init(void *arg)
{
	struct lininoio_data *data = arg;
	const struct lininoio_channel *c, *_c;
	int stat;

	memset(data, 0, sizeof(*data));
	data->state = UNASSOCIATED;
	data->io_pipe = pipe_open(CONFIG_LININOIO_PIPE_PATH,
				  BATHOS_MODE_INPUT_OUTPUT | BATHOS_MODE_ASYNC,
				  &global_data);
	if (!data->io_pipe) {
		printf("%s: error opening io_pipe\n", __func__);
		return -ENODEV;
	}
	pipe_remap_buffer_available_event(data->io_pipe,
					  &event_name(lininoio_buf_available));
	pipe_remap_buffer_processed_event(data->io_pipe,
					  &event_name(lininoio_buf_processed));
	stat = pipe_async_start(data->io_pipe);
	if (stat < 0)
		printf("%s: error starting io_pipe\n", __func__);
	for (_c = lininoio_channels_start; _c != lininoio_channels_end; _c++) {
		struct lininoio_channel_ops __ops;
		struct lininoio_channel __c;
		const struct lininoio_channel_ops *ops;

		c = __get_channel(_c, &__c);
		ops = __get_channel_ops(c, &__ops);
		if (ops->init)
			ops->init(c, data->io_pipe);
	}
	return stat;
}

static void __association_request(struct bathos_bdescr *b,
				  struct lininoio_data *data)
{
	struct lininoio_arequest_packet *p;
	const struct lininoio_channel *c, *_c;
	int i;

	p = b->data;
	/* Setup association request */
	p->type = LININOIO_PACKET_AREQUEST;
	sprintf((char *)p->slave_name, "%s", CONFIG_LININOIO_SLAVE_NAME);
	p->nchannels = lininoio_channels_end - lininoio_channels_start;
	for (i = 0, _c = lininoio_channels_start; _c != lininoio_channels_end;
	     _c++, i++) {
		struct lininoio_channel __c;
		const struct lininoio_channel_descr *d;
		struct lininoio_channel_descr __d;

		c = __get_channel(_c, &__c);
		d = __get_channel_descr(c, &__d);
		p->chan_descr[i] = d->contents_id;
	}
	b->data_size = sizeof(*p) + sizeof(p->chan_descr[0]) * (i + 1);
}

static void __send_alive(struct bathos_bdescr *b,
			 struct lininoio_data *data)
{
	struct lininoio_data_packet *p;

	p = b->data;
	p->type = LININOIO_PACKET_DATA;
	/* dlen 0, alive packet */
	p->cdlen = 0;
}

static struct bathos_bdescr *__get_buf(struct lininoio_data *data)
{
	struct bathos_bdescr *b = pipe_async_get_buf(data->io_pipe);

	if (!b) {
		printf("%s: no buffer available\n", __func__);
		return NULL;
	}
	if (!b->data) {
		printf("%s: b has NULL data ptr\n", __func__);
		return NULL;
	}
	return b;
}

static void *lininoio_periodic(void *arg)
{
	struct lininoio_data *data = arg;
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	static int started = 0;

	if (!data->io_pipe)
		return arg;

	if (!started) {
		started = 1;
		pipe_async_start(data->io_pipe);
	}

	b = __get_buf(data);
	if (!b)
		return arg;
	op = to_operation(b);

	switch (data->state) {
	case ASSOCIATING:
		/* Request sent, do nothing and bail out */
		return arg;
	case ASSOCIATED:
		if (data->activity && data->activity != -1)
			/*
			 * No alive message if activity detected during last
			 * period
			 */
			return arg;
		/*
		 * Send alive packet and reset activity counter to -1, because
		 * the counter will be incremented to 0 on tx completed
		 */
		__send_alive(b, data);
		data->activity = -1;
		op->type = SEND;
		memcpy(&op->addr, &data->host_address,
		       sizeof(data->host_address));
		break;
	case UNASSOCIATED:
		__association_request(b, data);
		/* Setup a "universal" broadcast address */
		memset(op->addr.val, 0xff, 6);
		/* HACK: also add the lininoio eth type */
		op->addr.val[6] = 0x86;
		op->addr.val[7] = 0xb5;
		op->type = SEND;
		break;
	default:
		break;
	}
	/* Release buffer to queue for being sent */
	bdescr_put(b);
	return arg;
}

static struct bathos_task __task t_lininoio = {
	.name = "lininoio", .period = 2 * HZ,
	.job = lininoio_periodic, .arg = &global_data,
	.init = lininoio_init,
	.release = 3,
};

/*
 * Buffer available event handler: this is called when the IO pipe has
 * space for processing I/O buffers
 * We keep a configurable amount of buffers constantly queued for being received
 */
static void lininoio_buf_available_handle(struct event_handler_data *ed)
{
	struct lininoio_data *data = ed->priv;

	for ( ; data->outstanding_rx_buffers < CONFIG_LININOIO_RX_BUFFERS;
	      data->outstanding_rx_buffers++) {
		struct bathos_bdescr *b;
		struct bathos_buffer_op *op;

		b = pipe_async_get_buf(data->io_pipe);
		if (!b)
			break;
		op = to_operation(b);
		op->type = RECV;
		/* Release buffer to queue for being received */
		bdescr_put(b);
	}
}

declare_event_handler_with_priv(lininoio_buf_available, NULL,
				lininoio_buf_available_handle,
				NULL, &global_data);

static void __process_areply(struct lininoio_data *data,
			     struct lininoio_areply_packet *p,
			     unsigned int sz)
{
	uint8_t cindex;
	uint16_t len;
	int stat, processed, nchannels =
		lininoio_channels_end - lininoio_channels_start;
	struct lininoio_association_data *adata;
	const struct lininoio_channel *c, *_c;

	if (p->status) {
		/* Association refused */
		printf("WARN: association refused (%d)\n", p->status);
		return;
	}
	for (processed = sizeof(*p), adata = p->adata; processed < sz;
	     processed += len + sizeof(p->adata), adata++) {
		struct lininoio_channel_ops __ops;
		struct lininoio_channel __c;
		const struct lininoio_channel_ops *ops;

		len = lininoio_decode_cdlen(adata->chan_dlen, &cindex);
		if (cindex >= nchannels) {
			printf("ERR: %s: invalid channel %u\n", __func__,
			       cindex);
			continue;
		}
		_c = &lininoio_channels_start[cindex];
		c = __get_channel(_c, &__c);
		ops = __get_channel_ops(c, &__ops);
		if (ops->setup) {
			stat = ops->setup(c, adata);
			if (stat < 0)
				printf("WARN: %s: setup error for chan %u\n",
				       __func__, cindex);
		}
	}
}

static void __process_data(struct lininoio_data *data,
			   struct lininoio_data_packet *p,
			   unsigned int sz)
{
	uint8_t cindex;
	uint16_t len;
	int stat, nchannels =
		lininoio_channels_end - lininoio_channels_start;
	const struct lininoio_channel *c, *_c;
	struct lininoio_channel_ops __ops;
	struct lininoio_channel __c;
	const struct lininoio_channel_ops *ops;

	len = lininoio_decode_cdlen(p->cdlen, &cindex);
	if (cindex >= nchannels) {
		printf("ERR: %s: invalid channel %u\n", __func__,
		       cindex);
		return;
	}
	_c = &lininoio_channels_start[cindex];
	c = __get_channel(_c, &__c);
	ops = __get_channel_ops(c, &__ops);
	if (ops->input) {
		stat = ops->input(c, data, len);
		if (stat < 0)
			printf("WARN: %s: input error for chan %u\n",
			       __func__, cindex);
	}
}

static void
__process_input(struct lininoio_data *data, struct bathos_bdescr *b)
{
	struct lininoio_packet *p = b->data;

	if (!p) {
		printf("WARN: %s, packet with no data\n", __func__);
		return;
	}
	switch(p->type) {
	case LININOIO_PACKET_AREPLY:
		__process_areply(data, b->data, b->data_size);
		break;
	case LININOIO_PACKET_DATA:
		__process_data(data, b->data, b->data_size);
		break;
	default:
		printf("WARN: %s, unexpected packet type %u\n", __func__,
		       p->type);
		break;
	}
	/*
	 * Release packet, process functions should get the buffer if
	 * they don't wan't it to be given back to the pipe at this point
	 */
	bdescr_put(b);
}

/*
 * Buffer processed handler: this is called when a buffer has been processed
 * by the IO pipe
 */
static void lininoio_buf_processed_handle(struct event_handler_data *ed)
{
	struct lininoio_data *data = ed->priv;
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op = to_operation(b);

	if (!b) {
		printf("WARN: %s, ed->data is NULL\n", __func__);
		return;
	}
	switch(op->type) {
	case SEND:
		data->activity++;
		break;
	case RECV:
		/* A buffer has been processed */
		__process_input(data, b);
		break;
	case NONE:
		printf("WARN: %s, buffer operation is NONE\n", __func__);
		break;
	}
}

declare_event_handler_with_priv(lininoio_buf_processed, NULL,
				lininoio_buf_processed_handle,
				NULL, &global_data);
