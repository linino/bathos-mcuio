/*
 * Implementation of mqtt client functions, utilities and events
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#include <bathos/bathos.h>
#include <bathos/event.h>
#include <bathos/init.h>
#include <lwip/ip_addr.h>
#include <bathos/mqtt_pipe.h>
#include "mqtt-client-internal.h"

#define MAX_DATA_BUFFERS 16

declare_event(mqtt_buffer_available);
declare_event(mqtt_buffer_processed);

struct data_buffer {
	struct mqtt_publish_data pd;
	char *str;
	struct list_head list;
};

#define to_data_buffer(s) container_of(s, struct data_buffer, pd)

static struct list_head free_data_buffers;
static struct list_head busy_data_buffers;


struct data_buffer data_buffers[MAX_DATA_BUFFERS];

static inline struct data_buffer *_get_db(void)
{
	struct data_buffer *out = NULL;
	
	if (list_empty(&free_data_buffers))
		return out;
	out = list_first_entry(&free_data_buffers, struct data_buffer, list);
	list_move(&out->list, &busy_data_buffers);
	return out;
}

static inline void _put_db(struct data_buffer *db)
{
	list_move(&db->list, &free_data_buffers);
}

static int _init_db(void)
{
	int i;

	INIT_LIST_HEAD(&free_data_buffers);
	INIT_LIST_HEAD(&busy_data_buffers);

	for (i = 0; i < ARRAY_SIZE(data_buffers); i++)
		list_add(&data_buffers[i].list, &free_data_buffers);

	return 0;
}
core_initcall(_init_db);

static void mqtt_buffer_available_handle(struct event_handler_data *d)
{
	/* Actually unused at the moment */
}
declare_event_handler(mqtt_buffer_available, NULL,
		      mqtt_buffer_available_handle, NULL);

static void mqtt_buffer_processed_handle(struct event_handler_data *d)
{
	struct bathos_bdescr *b = d->data;
	struct data_buffer *db = to_data_buffer(b->data);

	bathos_free_buffer(db->pd.data, db->pd.data_len);
	_put_db(db);
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(mqtt_buffer_processed, NULL,
		      mqtt_buffer_processed_handle, NULL);

struct mqtt_bathos_client *
bathos_mqtt_publisher_init(const struct mqtt_client_data *_mcd,
			   const struct event * PROGMEM publisher_ready_event,
			   const struct event * PROGMEM publisher_error_event)
{
	struct bathos_pipe *mqtt_pipe;
	struct mqtt_client_data *mcd = bathos_alloc_buffer(sizeof(*mcd));

	if (!mcd) {
		printf("%s: no memory for client data\n", __func__);
		return NULL;
	}
	*mcd = *_mcd;

	mcd->available_event = &event_name(mqtt_buffer_available);
	mcd->connected_event = publisher_ready_event;
	mcd->connection_error_event = publisher_error_event;

	mqtt_pipe = pipe_open("mqtt-dev",
			      BATHOS_MODE_OUTPUT|BATHOS_MODE_ASYNC,
			      mcd);
	if (!mqtt_pipe) {
		printf("error opening mqtt pipe\n");
		bathos_free_buffer(mcd, sizeof(*mcd));
		return NULL;
	}

	pipe_remap_buffer_available_event(mqtt_pipe,
					  &event_name(mqtt_buffer_available));
	pipe_remap_buffer_processed_event(mqtt_pipe,
					  &event_name(mqtt_buffer_processed));
	if (pipe_async_start(mqtt_pipe) < 0) {
		printf("ERROR starting mqtt pipe\n");
		pipe_close(mqtt_pipe);
		bathos_free_buffer(mcd, sizeof(*mcd));
		return NULL;
	}

	return mqtt_pipe->dev->ll_priv;
}


int bathos_mqtt_publish(struct mqtt_bathos_client *client,
			char *topic, void *_data, int data_len, int flags)
{
	struct bathos_bqueue *bq;
	struct bathos_bdescr *bdescr;
	struct data_buffer *db;
	void *data;
	int ret = 0;

	bq = bathos_dev_get_bqueue(client->mqtt_pipe);
	
	if (!bq) {
		printf("%s: no buffer queue\n", __func__);
		return -EINVAL;
	}
	db = _get_db();
	if (!db) {
		printf("%s: no data buffer available\n", __func__);
		return -ENOMEM;
	}
	bdescr = bathos_bqueue_get_buf(bq);
	if (!bdescr) {
		printf("%s: no buffer descriptor available\n", __func__);
		ret = -ENOMEM;
		goto error0;
	}
	data = bathos_alloc_buffer(data_len);
	if (!data) {
		printf("%s: no memory available for data\n", __func__);
		ret = -ENOMEM;
		goto error1;
	}
	memcpy(data, _data, data_len);
	
	bdescr->data = &db->pd;
	bdescr->data_size = sizeof(db->pd);
	db->pd.magic = MQTT_PUBLISH_MAGIC;
	db->pd.topic = topic;
	db->pd.data = data;
	db->pd.data_len = data_len;
	/* Buffer descriptor ready, pass it to the pipe for tx */
	bdescr_put(bdescr);
	return ret;

error1:
	bathos_bqueue_server_buf_done(bdescr);
error0:
	_put_db(db);
	return ret;
}

int bathos_mqtt_publisher_fini(const struct mqtt_bathos_client *client)
{
	int ret;
	struct mqtt_client_data *mcd = client->mqtt_pipe->data;

	ret = pipe_close(client->mqtt_pipe);
	if (ret < 0)
		return ret;
	bathos_free_buffer(mcd, sizeof(*mcd));
	return ret;
}
