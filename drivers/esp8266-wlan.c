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
	struct bathos_pipe *pipe;
	const struct esp8266_wlan_platform_data *plat;
	struct list_head rx_queue;
	struct list_head tx_queue;
	void *buffer_area;
	struct bathos_dev_data *dev_data;
};

declare_event(esp8266_wlan_setup);
declare_event(esp8266_wlan_done);

static void start_tx(struct esp8266_wlan_priv *priv)
{
	struct netif *netif = (struct netif *)eagle_lwip_getif(0);
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	struct pbuf *pbuf;
	uint8_t mac[6];

	wifi_get_macaddr(STATION_IF, mac);
	if (!netif) {
		printf("ERR: %s, could not get interface\n", __func__);
		return;
	}
	if (list_empty(&priv->tx_queue))
		/* Nothing in list, should not happen */
		return;
	b = list_first_entry(&priv->tx_queue, struct bathos_bdescr, list);
	op = to_operation(b);
	pbuf = pbuf_alloc(PBUF_RAW, b->data_size + 14, PBUF_RAM);
	if (!pbuf) {
		printf("ERR: %s, error in pbuf allocation\n", __func__);
		return;
	}
	printf("%s: sending packet\n", __func__);
	/* FIXME: AVOID COPIES ? */
	/* Copy destination address */
	memcpy(pbuf->payload, op->addr.val, 6);
	/* Copy source address */
	memcpy(pbuf->payload + 6, mac, 6);
	/* HACK: Copy ethernet type from dest address */
	memcpy(pbuf->payload + 12, &op->addr.val[6], 2);
	/* Copy rest of packet */
	memcpy(pbuf->payload + 14, b->data, b->data_size);
	{
		int i;
		unsigned char *ptr = b->data;

		for (i = 0; i < b->data_size; i++)
			printf("0x%02x ", ptr[i]);
	}
	ieee80211_output_pbuf(netif, pbuf);
	bathos_bqueue_server_buf_done(b);
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


static void wifi_callback(System_Event_t *evt)
{
	static struct ETSEventTag *queue;

	printf( "%s: %d\n", __FUNCTION__, evt->event );
	switch ( evt->event )
	{
	case EVENT_STAMODE_CONNECTED:
	{
		printf("connect to ssid %s, channel %d\n",
		       evt->event_info.connected.ssid,
		       evt->event_info.connected.channel);
		queue = bathos_alloc_buffer(sizeof(struct ETSEventTag) *
					    QUEUE_LEN);
		if (!queue) {
			printf("could not allocate queue\n");
			wifi_station_disconnect();
			return;
		}
		ets_task(raw_input_task, RAW_INPUT_TASK_PRIO, queue, QUEUE_LEN);
		break;
	}

	case EVENT_STAMODE_DISCONNECTED:
	{
		printf("disconnect from ssid %s, reason %d\n",
		       evt->event_info.disconnected.ssid,
		       evt->event_info.disconnected.reason);
		break;
	}

	case EVENT_STAMODE_GOT_IP:
	{
		printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
		       IP2STR(&evt->event_info.got_ip.ip),
		       IP2STR(&evt->event_info.got_ip.mask),
		       IP2STR(&evt->event_info.got_ip.gw));
		printf("\n");
		break;
	}
	default:
	{
		break;
	}
	}
}

static int esp8266_wlan_async_open(void *_priv)
{
	int ret;
	struct esp8266_wlan_priv *priv = _priv;
	const struct esp8266_wlan_platform_data *plat = priv->plat;
	struct bathos_dev *dev = priv->pipe->dev;
	struct bathos_bqueue *q = dev->ops->get_bqueue ?
	    dev->ops->get_bqueue(priv->pipe) : NULL;
	static struct station_config config;

	if (!q)
		return -EINVAL;
	ret = bathos_bqueue_server_init(q,
					&event_name(esp8266_wlan_setup),
					&event_name(esp8266_wlan_done),
					priv->buffer_area,
					plat->nbufs,
					plat->bufsize,
					REMOTE_MAC);
	if (ret)
		return ret;

	/* Init wifi */
	wifi_station_set_hostname(CONFIG_ESP8266_STATION_HOSTNAME);
	wifi_set_opmode_current(STATION_MODE);
	config.bssid_set = 0;
	memcpy(&config.ssid, CONFIG_ESP8266_ESSID, 32);
	memcpy(&config.password, CONFIG_ESP8266_PASSWD, 64);
	wifi_station_set_config(&config);
	wifi_set_event_handler_cb(wifi_callback);
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
	priv->dev_data = bathos_dev_init(&esp8266_wlan_ll_dev_ops, priv);
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


const struct bathos_dev_ops esp8266_wlan_dev_ops = {
	.open = esp8266_wlan_open,
	.get_bqueue = bathos_dev_get_bqueue,
};
