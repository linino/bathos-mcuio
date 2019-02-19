/*
 * tcp/ip client interface
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#ifndef __TCP_CLIENT_PIPE_H__
#define __TCP_CLIENT_PIPE_H__

#include <bathos/bathos.h>
#include <generated/autoconf.h>

struct tcp_conn_data;

struct tcp_client_data {
	struct ip_addr remote_addr;
	unsigned short port;
	/* Implementation private data, don't use this */
	void *impl_priv;
	const struct event *connected_event;
	/* Data to be passed with a connected_event */
	void *connected_event_data;
};

const struct bathos_dev_ops tcp_client_dev_ops;

#endif /* __TCP_CLIENT_PIPE_H__ */

