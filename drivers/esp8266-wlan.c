/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* send/receive raw packets over wlan on the esp8266 via async interface */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/buffer_queue_server.h>
#include <bathos/esp8266-wlan.h>
#include <lwip/pbuf.h>
//#include <lwip/netif.h>
#include <netif/wlan_lwip_if.h>
#include <ets_sys.h>
#include <osapi.h>
#include <user_interface.h>

#define RAW_INPUT_TASK_PRIO 28
#define QUEUE_LEN 4

#define MAX_CUSTOM_PBUFS 128

struct esp8266_wlan_priv {
	const struct esp8266_wlan_platform_data *plat;
	struct list_head rx_queue;
	struct list_head tx_queue;
	void *buffer_area;
};

static uint8_t _buffer_area[1514*2];

/*
 * Can't have more than one open instance. Unfortunately an ets_task handler
 * cannot receive some void pointer for private data, so the easiest way to
 * work around this is having a global private data structure and allow for
 * one instance;
 */
static struct esp8266_wlan_priv _priv;

static struct ETSEventTag _queue[QUEUE_LEN];

declare_event(esp8266_wlan_setup);
declare_event(esp8266_wlan_done);
declare_event(esp8266_input_packet);

struct pbuf *_setup_pbuf_copy(struct bathos_bdescr *b,
			      struct bathos_buffer_op *op)
{
	int pbuf_size, pbuf_data_offset = op->addr.type == REMOTE_MAC ? 14 : 0;
	struct pbuf *pbuf;
	uint8_t mac[6];

	pr_debug("%s, b = %p\n", __func__, b);
	wifi_get_macaddr(STATION_IF, mac);
	pbuf_size = bdescr_data_size(b) + pbuf_data_offset;
	pbuf = pbuf_alloc(PBUF_RAW, pbuf_size, PBUF_RAM);
	if (!pbuf)
		return pbuf;
	if (op->addr.type == REMOTE_MAC) {
		/* Copy destination address */
		memcpy(pbuf->payload, op->addr.val.b, 6);
		/* Copy source address */
		memcpy(pbuf->payload + 6, mac, 6);
		/* HACK: Copy ethernet type from dest address */
		memcpy(pbuf->payload + 12, &op->addr.val.b[6], 2);
	}
	/* Copy rest of packet */
	bdescr_copy_lin(b, pbuf->payload + pbuf_data_offset);
#ifdef DEBUG
	{
		int i;
		unsigned char *ptr = pbuf->payload;

		for (i = 0; i < pbuf_size; i++)
			printf("0x%02x ", ptr[i]);
	}
#endif
	return pbuf;
}

struct my_pbuf_custom {
	struct pbuf_custom pc;
	struct bathos_bdescr *b;
	struct list_head list;
};

#define to_my_pbc(a) \
	container_of(a, struct my_pbuf_custom, pc.pbuf)

struct list_head free_custom_pbufs;

static void _free_pbuf(struct pbuf *pbuf)
{
	struct my_pbuf_custom *pbc;

	if (!pbuf)
		return;
	pbc = to_my_pbc(pbuf);
	if (pbc->b) {
		bathos_bqueue_server_buf_processed(pbc->b);
		pbc->b = NULL;
	}
	list_add_tail(&pbc->list, &free_custom_pbufs);
};

static struct my_pbuf_custom custom_pbufs[MAX_CUSTOM_PBUFS] = {
	[0 ... MAX_CUSTOM_PBUFS - 1] = {
		.pc.custom_free_function = _free_pbuf,
	},
};

static struct my_pbuf_custom *_get_pbc(void)
{
	struct my_pbuf_custom *out = NULL;

	if (list_empty(&free_custom_pbufs))
		return NULL;
	out = list_first_entry(&free_custom_pbufs, struct my_pbuf_custom, list);
	list_del_init(&out->list);
	return out;
}

struct pbuf *_setup_pbuf_zerocopy(struct bathos_bdescr *b)
{
	struct my_pbuf_custom *pbc;
	struct pbuf *out = NULL;

	pbc = _get_pbc();
	if (!pbc) {
		printf("%s: NO FREE pbuf\n", __func__);
		_free_pbuf(out);
		b->error = -ENOMEM;
		return NULL;
	}
	out = pbuf_alloced_custom(PBUF_RAW, b->data_size, PBUF_RAM,
				  &pbc->pc, b->data, b->data_size);
	if (!out) {
		printf("%s: pbuf_alloced_custom returns error\n", __func__);
		_free_pbuf(out);
		b->error = -EINVAL;
		return NULL;
	}
	pbc->b = b;
	return out;
}

static struct pbuf *_setup_pbuf(struct bathos_bdescr *b, int *free_now)
{
	struct bathos_buffer_op *op = to_operation(b);
	struct pbuf *out;

	if (op->addr.type == REMOTE_MAC || !b->data) {
		return _setup_pbuf_copy(b, op);
		*free_now = 1;
	}
	out = _setup_pbuf_zerocopy(b);
	*free_now = out == NULL;
	return out;
}


static void start_tx(struct esp8266_wlan_priv *priv)
{
	struct netif *netif = (struct netif *)eagle_lwip_getif(0);
	struct bathos_bdescr *b;
	struct pbuf *pbuf = NULL;
	int free_now;

	if (!netif) {
		printf("ERR: %s, could not get interface\n", __func__);
		return;
	}
	if (list_empty(&priv->tx_queue))
		/* Nothing in list, should not happen */
		return;
	b = list_first_entry(&priv->tx_queue, struct bathos_bdescr, list);
	pbuf = _setup_pbuf(b, &free_now);
	ieee80211_output_pbuf(netif, pbuf);
	pbuf_free(pbuf);
	list_del_init(&b->list);
	if (free_now)
		bathos_bqueue_server_buf_processed(b);
}

/*
 * This is invoked when a buffer has been setup by the client and is
 * ready for tx/rx submission
 */
static void esp8266_wlan_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;
	struct bathos_bqueue *q;
	struct esp8266_wlan_priv *priv;

	pr_debug("%s, buffer = %p\n", __func__, b);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	b->driver_data = NULL;
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
declare_event_handler(esp8266_wlan_setup, NULL, esp8266_wlan_setup_handler,
		      NULL);


/*
 * This is invoked when a buffer has been released by the client
 */
static void esp8266_wlan_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;

	pr_debug("%s, buffer %p\n", __func__, b);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	op = to_operation(b);
	if (op->type == RECV) {
		if (b->driver_data) {
			/* Zero copy recv, free relevant pbuf */
			struct pbuf *p = b->driver_data;

			pbuf_free(p);
		} else
			/* memcpy recv, free data area */
			bathos_free_buffer(b->data, b->data_size);
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(esp8266_wlan_done, NULL, esp8266_wlan_done_handler,
		      NULL);

static int _setup_rx_buf_copy(struct pbuf *p, struct bathos_bdescr *b,
			      int total_length)
{
	struct pbuf *ptr;
	int copied;

	b->data = bathos_alloc_buffer(total_length);
	if (!b->data)
		return -ENOMEM;
	b->driver_data = NULL;
	for (ptr = p, copied = 0; ; ptr = ptr->next) {
		memcpy(&((uint8_t *)b->data)[copied], ptr->payload, ptr->len);
		pr_debug("%s: copied %d bytes\n", __func__, ptr->len);
		copied += ptr->len;
		if (ptr->tot_len == ptr->len)
			break;
	}
	b->data_size = copied;
	pbuf_free(p);
	return copied;
}

static int _setup_rx_buf_zerocopy(struct pbuf *p, struct bathos_bdescr *b,
				  int total_data_length)
{
	b->data = p->payload;
	b->data_size = p->len;
	b->driver_data = p;
	/* We don't free p here, since we're holding a reference to it */
	return b->data_size;
}

static void esp8266_input_packet_handler(struct event_handler_data *ed)
{
	struct esp8266_wlan_priv *priv = &_priv;
	struct bathos_bdescr *b;
	struct pbuf *p = ed->data, *ptr;
	int total_length, nfragments, stat;

	pr_debug("%s, pbuf = %p\n", __func__, p);
	if (list_empty(&priv->rx_queue)) {
		printf("%s: empty rx queue, throwing away\n", __func__);
		pbuf_free(p);
		return;
	}
	b = list_first_entry(&priv->rx_queue, struct bathos_bdescr, list);
	/* Determine total length and number of fragments */
	for (ptr = p, total_length = 0, nfragments = 1; ;
	     ptr = ptr->next, nfragments++, total_length += ptr->len)
		if (ptr->tot_len == ptr->len)
			break;
	stat = 0;
	if (nfragments == 1) {
		pr_debug("JUST ONE FRAGMENT, zerocopy\n");
		stat = _setup_rx_buf_zerocopy(p, b, total_length);
	}
	if (stat < 0 || nfragments > 1) {
		pr_debug("MORE THAN ONE FRAGMENT OR ZEROCOPY FAILED, memcpy\n");
		stat = _setup_rx_buf_copy(p, b, total_length);
	}
	if (stat < 0)
		b->error = stat;
	list_del_init(&b->list);
	bathos_bqueue_server_buf_processed(b);
}
declare_event_handler(esp8266_input_packet, NULL, esp8266_input_packet_handler,
		      NULL);

static void raw_input_task(struct ETSEventTag *e)
{
	struct pbuf *p = (struct pbuf *)(e->par);

	if (trigger_event(&evt_esp8266_input_packet, p) < 0)
		printf("%s: events overflow\n", __func__);
}

/* Prototype is missing !? */
extern void ets_task(void (*t)(struct ETSEventTag *), int, struct ETSEventTag *,
		     int);


static void wifi_connected_event_handler(struct event_handler_data *ed)
{
	ets_task(raw_input_task, RAW_INPUT_TASK_PRIO,
		 _queue, ARRAY_SIZE(_queue));
}
declare_event_handler(wifi_connected, NULL,
		      wifi_connected_event_handler, NULL);

static int esp8266_wlan_async_open(void *_priv)
{
	return 0;
}

static const struct bathos_ll_dev_ops esp8266_wlan_ll_dev_ops = {
	.async_open = esp8266_wlan_async_open,
};


static int esp8266_wlan_open(struct bathos_pipe *pipe)
{
	struct esp8266_wlan_priv *priv;
	struct bathos_dev *dev = pipe->dev;
	const struct esp8266_wlan_platform_data *plat = dev->platform_data;
	int ret, i;

	if (!pipe_mode_is_async(pipe))
		return -EINVAL;
	priv = &_priv;
	if (priv->plat) {
		printf("JUST ONE OPEN INSTANCE ALLOWED FOR THIS DRIVER\n");
		return -ENOMEM;
	}
	priv->plat = plat;
	INIT_LIST_HEAD(&priv->rx_queue);
	INIT_LIST_HEAD(&priv->tx_queue);
	INIT_LIST_HEAD(&free_custom_pbufs);
	for (i = 0; i < MAX_CUSTOM_PBUFS; i++)
		list_add_tail(&custom_pbufs[i].list, &free_custom_pbufs);
	priv->buffer_area = _buffer_area;
	pipe->dev_data = bathos_dev_init(dev, &esp8266_wlan_ll_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENOMEM;
		goto error0;
	}
	ret = bathos_dev_open(pipe);
	if (ret < 0)
		goto error1;
	if (pipe->mode & BATHOS_MODE_ASYNC) {
		struct bathos_bqueue *q = dev->ops->get_bqueue ?
			dev->ops->get_bqueue(pipe) : NULL;
		struct event *se = &event_name(esp8266_wlan_setup);
		struct event *de = &event_name(esp8266_wlan_done);

		if (!q)
			goto error1;
		ret = bathos_bqueue_server_init_dir(q,
						    se,
						    de,
						    NULL,
						    /*
						     * Two "empty" buffers
						     * for tx
						     */
						    16,
						    0,
						    REMOTE_MAC,
						    OUT);
		if (ret < 0)
			goto error1;
		ret = bathos_bqueue_server_init_dir(q,
						    se,
						    de,
						    priv->buffer_area,
						    2,
						    1514,
						    REMOTE_MAC,
						    IN);
	}
	if (ret)
		goto error1;
	return ret;

error1:
	bathos_dev_uninit(pipe);
error0:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}

static int esp8266_wlan_close(struct bathos_pipe *pipe)
{
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(pipe);
	struct esp8266_wlan_priv *priv = bathos_bqueue_to_ll_priv(bq);
	struct bathos_dev *dev = pipe->dev;
	const struct esp8266_wlan_platform_data *plat = dev->platform_data;
	struct bathos_bdescr *b, *tmp;

	/* Tell the client we're closing */
	list_for_each_entry_safe(b, tmp, &priv->rx_queue, list) {
		/* buffer queue is being killed */
		b->error = -EPIPE;
		bathos_bqueue_server_buf_processed_immediate(b);
	}
	list_for_each_entry_safe(b, tmp, &priv->tx_queue, list) {
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
	bathos_free_buffer(priv, sizeof(*priv));
	return 0;
}

const struct bathos_dev_ops esp8266_wlan_dev_ops = {
	.open = esp8266_wlan_open,
	.get_bqueue = bathos_dev_get_bqueue,
	.close = esp8266_wlan_close,
};
