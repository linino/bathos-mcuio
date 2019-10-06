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

static void start_tx(struct esp8266_wlan_priv *priv)
{
	struct netif *netif = (struct netif *)eagle_lwip_getif(0);
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	struct pbuf *pbuf = NULL;
	uint8_t mac[6];
	int data_size, pbuf_size, pbuf_data_offset = 0;

	wifi_get_macaddr(STATION_IF, mac);
	if (!netif) {
		printf("ERR: %s, could not get interface\n", __func__);
		return;
	}
	if (list_empty(&priv->tx_queue))
		/* Nothing in list, should not happen */
		return;
	b = list_first_entry(&priv->tx_queue, struct bathos_bdescr, list);
	pbuf_size = data_size = bdescr_data_size(b);
	pr_debug("pbuf_size = %d\n", pbuf_size);
	op = to_operation(b);
	if (op->addr.type == REMOTE_MAC) {
		pbuf_data_offset = 14;
		pbuf_size += pbuf_data_offset;
	}
	pbuf = pbuf_alloc(PBUF_RAW, pbuf_size, PBUF_RAM);
	if (!pbuf) {
		printf("ERR: %s, error in pbuf allocation\n", __func__);
		b->error = -ENOMEM;
		goto end;
	}
	pr_debug("%s: sending packet, pbuf = %p\n", __func__, pbuf);
	if (op->addr.type == REMOTE_MAC) {
		/* FIXME: AVOID COPIES ? */
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

		for (i = 0; i < data_size; i++)
			printf("0x%02x ", ptr[i]);
	}
#endif
	ieee80211_output_pbuf(netif, pbuf);
	pbuf_free(pbuf);
end:
	list_del_init(&b->list);
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
declare_event_handler(esp8266_wlan_setup, NULL, esp8266_wlan_setup_handler,
		      NULL);


/*
 * This is invoked when a buffer has been released by the client
 */
static void esp8266_wlan_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(esp8266_wlan_done, NULL, esp8266_wlan_done_handler,
		      NULL);

static void raw_input_task(struct ETSEventTag *e)
{
	struct pbuf *ptr = (struct pbuf *)(e->par);
	uint8_t buf[16];
	int  i;

	printf("%s, pbuf = %p\n", __func__, ptr);
	memcpy(buf, ptr->payload, sizeof(buf));
	for (i = 0; i < sizeof(buf); i++)
		printf("0x%02x ", buf[i]);
	printf("\n");
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
	int ret;

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
						    2,
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
