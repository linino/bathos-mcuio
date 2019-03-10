/*
 * mqtt client implementation
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#include <bathos/bathos.h>
#include <linux/list.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/buffer_queue_server.h>
#include <bathos/tcp-client-lwip-raw.h>
#include <bathos/tcp-client-pipe.h>
#include <bathos/mqtt_pipe.h>
#include <bathos/sys_timer.h>
#include <bathos/mqtt.h>

#define MAX_CLIENTS 4

#define MQTT_SENDBUF_SIZE 512
#define MQTT_RECVBUF_SIZE 512

struct mqtt_bathos_client {
	uint8_t *sendbuf;
	uint8_t *recvbuf;
	struct mqtt_client c;
	/* Related mqtt_pipe */
	struct bathos_pipe *mqtt_pipe;
	/* Related tcp/ip client pipe */
	struct bathos_pipe *tcpip_pipe;
	/* Pointer to buffer queue area (client side) */
	uint8_t *buffer_area;
	struct mqtt_client_data *cdata;
	int closing;
	/* Subscribe queue */
	struct list_head subscribe_queue;
	struct list_head list;
};

declare_event(mqtt_client_setup);
declare_event(mqtt_client_done);
declare_event(mqtt_broker_connected);
declare_event(mqtt_broker_connection_error);
declare_event(mqtt_sync_event);

static struct list_head mqtt_free_clients;
static struct list_head mqtt_busy_clients;

static struct mqtt_bathos_client clients[MAX_CLIENTS];

/* publish response callback */
static void prcb(void **state, struct mqtt_response_publish *pub)
{
	struct bathos_bdescr *b;
	struct mqtt_publish_data *pd;
	struct mqtt_bathos_client *client = (struct mqtt_bathos_client *)*state;
	
	if (list_empty(&client->subscribe_queue)) {
		printf("publish cb: no available buffers\n");
		return;
	}
	b = list_first_entry(&client->subscribe_queue, typeof(*b), list);
	pd = b->data;
	/*
	 * Put both the topic and the application message in the same buffer
	 */
	pd->topic = bathos_alloc_buffer(pub->topic_name_size + 1 +
					pub->application_message_size);

	if (!pd->topic) {
		printf("publish cb: not enough memory for topic and data");
		return;
	}
	/* Copy and zero terminate topic name string */
	memcpy(pd->topic, pub->topic_name, pub->topic_name_size);
	pd->topic[pub->topic_name_size] = '\0';
	/* Copy application message data */
	pd->data = &pd->topic[pub->topic_name_size + 1];
	memcpy(pd->data, pub->application_message,
	       pub->application_message_size);
	/* OK, buffer is now ready */
	bathos_bqueue_server_buf_processed(b);
}

static void mqtt_broker_connected_event_handler(struct event_handler_data *ed)
{
	int stat;
	struct mqtt_bathos_client *client = ed->data;

	/* 200ms */
	stat = sys_timer_enqueue_tick(HZ / 50, client,
				      &event_name(mqtt_sync_event));
	if (stat)
		printf("%s: could not setup sync event\n", __func__);
	trigger_event(client->cdata->connected_event, client->cdata);
}
declare_event_handler(mqtt_broker_connected, NULL,
		      mqtt_broker_connected_event_handler, NULL);

static void
mqtt_broker_connection_error_event_handler(struct event_handler_data *ed)
{
	struct mqtt_bathos_client *client = ed->data;

	trigger_event(client->cdata->connection_error_event, client->cdata);
}
declare_event_handler(mqtt_broker_connection_error, NULL,
		      mqtt_broker_connection_error_event_handler, NULL);

static void mqtt_sync_event_handler(struct event_handler_data *ed)
{
	struct mqtt_bathos_client *client = ed->data;
	int stat;

	if (!client) {
		printf("%s: no pointer to client\n", __func__);
		return;
	}
	if (client->closing) {
		client->closing = 0;
		list_move(&client->list, &mqtt_free_clients);
		return;
	}
	mqtt_sync(&client->c);
	/* Next sync in 200ms */
	stat = sys_timer_enqueue_tick(HZ / 50, client,
				      &event_name(mqtt_sync_event));
	if (stat)
		printf("%s: WARNING: could not restart sync event\n", __func__);
}

declare_event_handler(mqtt_sync_event, NULL, mqtt_sync_event_handler,
		      NULL);

static enum MQTTErrors _inspector_cb(struct mqtt_client *client)
{
	if (client->error != MQTT_OK) {
		printf("CLEARING CLIENT ERROR\n");
		client->error = MQTT_OK;
	}
	return MQTT_OK;		
}

static void _free_client(struct mqtt_bathos_client *c)
{
	struct mqtt_client_data *cdata = c->mqtt_pipe->data;

	pipe_close(c->tcpip_pipe);
	bathos_free_buffer(c->sendbuf, MQTT_SENDBUF_SIZE);
	bathos_free_buffer(c->recvbuf, MQTT_RECVBUF_SIZE);
	bathos_free_buffer(c->buffer_area, cdata->nbufs * cdata->bufsize);
	/* Will actually be freed on next scheduled tick */
	c->closing = 1;
}

static struct mqtt_bathos_client *_new_client(struct bathos_pipe *pipe)
{
	struct mqtt_bathos_client *out;
	enum MQTTErrors err;
	struct mqtt_client_data *cdata = pipe->data;
	struct tcp_client_data *tcp_cdata = &cdata->tcp_cdata;

	if (list_empty(&mqtt_free_clients)) {
		printf("%s: no free clients available\n", __func__);
		return NULL;
	}

	out = list_first_entry(&mqtt_free_clients, struct mqtt_bathos_client,
			       list);

	INIT_LIST_HEAD(&out->subscribe_queue);
	tcp_cdata->connected_event = &event_name(mqtt_broker_connected);
	tcp_cdata->connected_event_data = out;
	tcp_cdata->connection_error_event =
	    &event_name(mqtt_broker_connection_error);
	tcp_cdata->connection_error_event_data = out;
	out->cdata = cdata;
	out->c.publish_response_callback_state = out;
	out->c.inspector_callback = _inspector_cb;
	out->mqtt_pipe = pipe;

	out->sendbuf = bathos_alloc_buffer(MQTT_SENDBUF_SIZE);
	if (!out->sendbuf) {
		printf("%s: no memory for send buffer\n", __func__);
		return NULL;
	}

	out->recvbuf = bathos_alloc_buffer(MQTT_RECVBUF_SIZE);
	if (!out->recvbuf) {
		printf("%s: no memory for recv buffer\n", __func__);
		goto error0;
	}

	out->tcpip_pipe = pipe_open("tcp-client-connection",
				    BATHOS_MODE_INPUT|BATHOS_MODE_OUTPUT,
				    tcp_cdata);
	if (!out->tcpip_pipe) {
		printf("%s: couldn't open tcp ip client pipe\n", __func__);
		goto error1;
	}

	err = mqtt_init(&out->c, out->tcpip_pipe, out->sendbuf,
			MQTT_SENDBUF_SIZE, out->recvbuf,
			MQTT_RECVBUF_SIZE, prcb);

	if (err != MQTT_OK) {
		printf("%s: mqtt_init() returns %s\n", __func__,
		       mqtt_error_str(err));
		goto error2;
	}

	out->buffer_area = bathos_alloc_buffer(cdata->nbufs * cdata->bufsize);
	if (!out->buffer_area) {
		printf("%s: not enough memory for client buffer area\n",
		       __func__);
		goto error2;
	}

	list_move(&out->list, &mqtt_busy_clients);

	return out;

error2:
	pipe_close(out->tcpip_pipe);
error1:
	bathos_free_buffer(out->recvbuf, MQTT_RECVBUF_SIZE);
error0:
	bathos_free_buffer(out->sendbuf, MQTT_SENDBUF_SIZE);
	return NULL;
}

static void mqtt_client_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_bqueue *q;
	struct mqtt_bathos_client *client;
	struct mqtt_publish_data *pd;
	enum MQTTErrors err;

	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	q = b->queue;
	client = bathos_bqueue_to_ll_priv(q);
	if (!client) {
		printf("%s: ERR, client is NULL\n", __func__);
		b->error = -ENODEV;
		goto end;
	}
	b->error = 0;
	if (client->mqtt_pipe->mode & BATHOS_MODE_INPUT) {
		printf("%s %d\n", __func__, __LINE__);
		/* Subscriber, just add buffer to subscribe queue */
		list_move_tail(&b->list, &client->subscribe_queue);
		return;
	}
	pd = b->data;
	if (pd->magic != MQTT_PUBLISH_MAGIC) {
		printf("%s invalid buffer data\n", __func__);
		b->error = -EINVAL;
		goto end;
	}
	/* Publisher, do it */
	err = mqtt_publish(&client->c, pd->topic, pd->data,
			   pd->data_len, pd->flags);
	if (err != MQTT_OK) {
		printf("%s, mqtt_publish(): %s\n", __func__,
		       mqtt_error_str(err));
		b->error = err;
	}	
end:
	bathos_bqueue_server_buf_processed(b);
}
declare_event_handler(mqtt_client_setup, NULL, mqtt_client_setup_handler,
		      NULL);

static int mqtt_client_async_open(void *_client)
{
	struct mqtt_bathos_client *client = _client;
	struct mqtt_client_data *cdata;
	enum MQTTErrors err;

	cdata = client->cdata;

	err = mqtt_connect(&client->c, cdata->client_id, cdata->will_topic,
			   cdata->will_message, cdata->will_message_size,
			   cdata->user_name, cdata->password,
			   cdata->connect_flags, cdata->keep_alive);

	if (err != MQTT_OK) {
		printf("%s: mqtt_connect() returned error\n", __func__);
		return -EINVAL;
	}

	if (client->mqtt_pipe->mode & BATHOS_MODE_INPUT)
		mqtt_subscribe(&client->c, cdata->topic, 0);

	return 0;
}


/* Only write supported at the moment, no ioctl */
static const struct bathos_ll_dev_ops ll_ops = {
	.async_open = mqtt_client_async_open,
};

static int mqtt_client_dev_open(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;
	struct mqtt_bathos_client *client;
	struct mqtt_client_data *cd = pipe->data;
	struct bathos_bqueue *q;
	int ret;

	if ((pipe->mode & BATHOS_MODE_INPUT_OUTPUT) ==
	    BATHOS_MODE_INPUT_OUTPUT) {
		printf("%s: dev must be opened either for input or output\n",
		       __func__);
		return -EINVAL;
	}
	if (!pipe_mode_is_async(pipe)) {
		printf("%s: pipe must be opened in async mode\n", __func__);
		/* Only async mode is allowed */
		return -EINVAL;
	}
	if (!dev->ops->get_bqueue)
		return -EINVAL;

	client = _new_client(pipe);
	if (!client) {
		printf("%s: cannot create new client\n", __func__);
		return -ENOMEM;
	}

	pipe->dev_data = bathos_dev_init(dev, &ll_ops, client);
	if (!pipe->dev_data) {
		printf("%s: bathos_dev_init() returns error\n", __func__);
		ret = -ENODEV;
		goto error0;
	}
	ret = bathos_dev_open(pipe);
	if (ret < 0) {
		printf("%s: bathos_dev_open() returns error\n", __func__);
		goto error0;
	}
	q = bathos_dev_get_bqueue(pipe);
	if (!q) {
		printf("%s: cannot get buffer queue\n", __func__);
		return -EINVAL;
	}
	ret = bathos_bqueue_server_init(q,
					&event_name(mqtt_client_setup),
					&event_name(mqtt_client_done),
					client->buffer_area,
					cd->nbufs,
					cd->bufsize,
					DONTCARE);
	if (ret) {
		printf("%s: bathos_bqueue_server_init() returns error\n",
		       __func__);
		goto error1;
	}
	return ret;

error1:
	bathos_dev_uninit(pipe);
error0:
	_free_client(client);
	return ret;
}

int mqtt_client_on_pipe_close(struct bathos_pipe *pipe)
{
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(pipe);
	struct bathos_dev *dev = pipe->dev;
	struct mqtt_bathos_client *client = dev->ll_priv;

	pr_debug("%s, pipe = %p, client = %p\n", __func__, pipe, client);
	bathos_bqueue_stop(bq);
	bathos_bqueue_server_fini(bq);
	if (!client) {
		printf("%s: no ll_priv\n", __func__);
		return -EINVAL;
	}
	_free_client(client);
	bathos_dev_close(pipe);
	bathos_dev_uninit(pipe);
	return 0;
}


const struct bathos_dev_ops mqtt_client_dev_ops = {
	.open = mqtt_client_dev_open,
	.get_bqueue = bathos_dev_get_bqueue,
	.on_pipe_close = mqtt_client_on_pipe_close,
};

static int mqtt_clients_init(void)
{
	int i;

	INIT_LIST_HEAD(&mqtt_busy_clients);
	INIT_LIST_HEAD(&mqtt_free_clients);
	for (i = 0; i < ARRAY_SIZE(clients); i++)
		list_add(&clients[i].list, &mqtt_free_clients);
	return 0;
}

subsys_initcall(mqtt_clients_init);
