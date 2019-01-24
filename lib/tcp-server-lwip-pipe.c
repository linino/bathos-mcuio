/*
 * tcp/ip server pipe lwip implementation
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/tcp-server-lwip-raw.h>
#include <bathos/tcp-server-pipe.h>

#ifdef CONFIG_MAX_TCP_SERVERS
#define MAX_TCP_SERVERS CONFIG_MAX_TCP_SERVERS
#else
#define MAX_TCP_SERVERS 4
#endif

static int tcp_server_accept(struct tcp_conn_data *cd)
{
	/* Open an unnamed pipe and link it up to current connection */
	struct bathos_pipe *p = pipe_open("tcp-server-connection",
					  BATHOS_MODE_INPUT|BATHOS_MODE_OUTPUT,
					  cd);
	struct tcp_server_socket_lwip_raw *rs = cd->raw_socket;
	struct bathos_pipe *server_pipe = rs->pipe;
	struct tcp_server_data *data = server_pipe->data;

	if (!p) {
		printf("%s: cannot setup pipe for connection\n", __func__);
		return -ENOMEM;
	}
	cd->pipe = p;
	cd->can_close = 0;
	trigger_event(data->accept_event, p);
	return 0;
}

static int tcp_server_recv(struct tcp_conn_data *cd, const void *buf,
			   unsigned int len)
{
	/* Just push buffer to connection related pipe */
	return bathos_pipe_push_chars(cd->pipe, buf, len);
}

static int tcp_server_poll(struct tcp_conn_data *cd)
{
	/* Return 0 if connection can be closed */
	if (cd->state == ES_CLOSING) {
		/* Forcibly close pipe */
		bathos_pipe_push_chars(cd->pipe, NULL, 0);
		cd->can_close = 1;
	}
	return !cd->can_close;
}

static void tcp_server_conn_closed(struct tcp_conn_data *cd)
{
}


static const struct tcp_server_socket_lwip_raw_ops tcp_server_ops = {
	.accept = tcp_server_accept,
	.recv = tcp_server_recv,
	.poll = tcp_server_poll,
	.closed = tcp_server_conn_closed,
};

static struct tcp_server_socket_lwip_raw tcp_servers[MAX_TCP_SERVERS];

static struct tcp_server_socket_lwip_raw *_new_server(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tcp_servers); i++) {
		if (!tcp_servers[i].ops) {
			tcp_servers[i].ops = &tcp_server_ops;
			return &tcp_servers[i];
		}
	}
	return NULL;
}

static void _server_fini(struct tcp_server_socket_lwip_raw *rs)
{
	rs->ops = NULL;
}

static int tcp_server_dev_open(struct bathos_pipe *pipe)
{
	int stat;
	struct tcp_server_data *data = pipe->data;
	struct tcp_server_socket_lwip_raw *rs;

	if (!data->accept_event) {
		printf("%s: accept event is mandatory !\n", __func__);
		return -EINVAL;
	}
	rs = _new_server();
	if (!rs) {
		printf("%s: no more available raw sockets\n", __func__);
		return -ENOMEM;
	}
	rs->pipe = pipe;
	stat = tcp_server_socket_lwip_raw_init(rs, data->port);
	if (stat < 0) {
		printf("%s: error initializing raw socket\n", __func__);
		_server_fini(rs);
		return stat;
	}
	data->impl_priv = rs;
	return stat;
}

/* Read/write not supported on main server pipe */
static int tcp_server_dev_read(struct bathos_pipe *pipe, char *buf, int len)
{
	return -1;
}

static int tcp_server_dev_write(struct bathos_pipe *pipe, const char *buf,
				 int len)
{
	return -1;
}

/* No ioctl supported at the moment */
static int tcp_server_dev_ioctl(struct bathos_pipe *pipe,
				 struct bathos_ioctl_data *d)
{
	return -1;
}

static int tcp_server_dev_close(struct bathos_pipe *pipe)
{
	struct tcp_conn_data *cd = pipe->data;
	/* FIXME: Tear down connections here */

	return tcp_server_socket_lwip_raw_fini(cd->raw_socket);
}

const struct bathos_dev_ops tcp_server_dev_ops = {
	.open = tcp_server_dev_open,
	.read = tcp_server_dev_read,
	.write = tcp_server_dev_write,
	.close = tcp_server_dev_close,
	.ioctl = tcp_server_dev_ioctl,
};
