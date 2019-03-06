/*
 * mqtt client pipe
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#ifndef __MQTT_PIPE_H__
#define __MQTT_PIPE_H__

#include <bathos/bathos.h>
#include <bathos/tcp-client-pipe.h>
#include <generated/autoconf.h>

struct mqtt_client_data {
	struct tcp_client_data tcp_cdata;
	const char *client_id;
	const char *will_topic;
	const void *will_message;
	unsigned int will_message_size;
	const char *user_name;
	const char *password;
	/* subscribers only */
	const char *topic;
	uint8_t connect_flags;
	uint16_t keep_alive;
	unsigned int nbufs;
	unsigned int bufsize;
	const struct event *connected_event;
	const struct event *published_event;
	const struct event *available_event;
};

struct mqtt_publish_data {
#define MQTT_PUBLISH_MAGIC 0xdeadbeef	
	int magic;
	char *topic;
	void *data;
	int data_len;
	int flags;
};

const struct bathos_dev_ops mqtt_client_dev_ops;


#endif /* __MQTT_PIPE_H__ */
