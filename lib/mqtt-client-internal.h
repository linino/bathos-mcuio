/*
 * mqtt client private declarations
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#ifndef __MQTT_CLIENT_INTERNAL_H__
#define __MQTT_CLIENT_INTERNAL_H__

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
	int connected;
	/* Subscribe queue */
	struct list_head subscribe_queue;
	struct list_head list;
};


#endif /* __MQTT_CLIENT_INTERNAL_H__ */
