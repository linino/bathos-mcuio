/*
 * tcp/ip server interface
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#ifndef __TCP_SERVER_PIPE_H__
#define __TCP_SERVER_PIPE_H__

#include <bathos/bathos.h>
#include <generated/autoconf.h>

struct tcp_conn_data;

struct tcp_server_data {
	/* FIXME: LOCAL IP ADDRESS */
	unsigned short port;
	/* Implementation private data, don't use this */
	void *impl_priv;
	const struct event *accept_event;
};

int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *,
				    const void *buf, unsigned int len);

#endif /* __TCP_SERVER_PIPE_H__ */

