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
#include <bathos/tcp-client-lwip-raw.h>
#include <bathos/tcp-client-pipe.h>

#ifdef CONFIG_MAX_TCP_CLIENTS
#define MAX_TCP_CLIENTS CONFIG_MAX_TCP_CLIENTS
#else
#define MAX_TCP_CLIENTS 4
#endif

static const struct tcp_socket_lwip_raw_ops tcp_client_ops;

static struct tcp_socket_lwip_raw tcp_clients[MAX_TCP_CLIENTS];

static struct tcp_socket_lwip_raw *_new_client(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tcp_clients); i++) {
		if (!tcp_clients[i].ops) {
			tcp_clients[i].ops = &tcp_client_ops;
			return &tcp_clients[i];
		}
	}
	return NULL;
}

static void _client_fini(struct tcp_socket_lwip_raw *rs)
{
	rs->ops = NULL;
}


static int tcp_client_connected(struct tcp_conn_data *cd)
{
	struct tcp_socket_lwip_raw *rs = cd->raw_socket;
	struct tcp_client_data *data = rs->pipe->data;

	cd->pipe = rs->pipe;
	cd->can_close = 0;
	data->impl_priv = cd;
	if (data->connected_event)
		trigger_event(data->connected_event,
			      data->connected_event_data ?
			      data->connected_event_data : rs->pipe);
	return 0;
}

static int tcp_client_recv(struct tcp_conn_data *cd, const void *buf,
			   unsigned int len)
{
	/* Just push buffer to connection related pipe */
	return bathos_pipe_push_chars(cd->pipe, buf, len);
}

static int tcp_client_poll(struct tcp_conn_data *cd)
{
	/* Return 0 if connection can be closed */
	if (cd->state == ES_CLOSING) {
		/* Forcibly close pipe */
		bathos_pipe_push_chars(cd->pipe, NULL, 0);
		cd->can_close = 1;
	}
	return !cd->can_close;
}

static void tcp_client_conn_closed(struct tcp_conn_data *cd)
{
	struct tcp_socket_lwip_raw *rs = cd->raw_socket;

	_client_fini(rs);

	tcp_client_socket_lwip_raw_fini(cd->raw_socket);
}

static void tcp_client_conn_error(struct tcp_conn_data *cd, int error)
{
	struct tcp_socket_lwip_raw *rs = cd->raw_socket;
	struct tcp_client_data *data = rs->pipe->data;

	data->impl_priv = cd;
	if (data->connection_error_event)
		trigger_event(data->connection_error_event,
			      data->connection_error_event_data);
}

static const struct tcp_socket_lwip_raw_ops tcp_client_ops = {
	.connected = tcp_client_connected,
	.recv = tcp_client_recv,
	.poll = tcp_client_poll,
	.closed = tcp_client_conn_closed,
	.error = tcp_client_conn_error,
};

static const struct bathos_ll_dev_ops ll_ops = {
};

static int tcp_client_dev_open(struct bathos_pipe *pipe)
{
	int stat;
	struct tcp_client_data *data = pipe->data;
	struct tcp_socket_lwip_raw *rs;
	struct bathos_dev *dev = pipe->dev;

	rs = _new_client();
	if (!rs) {
		printf("%s: no more available raw sockets\n", __func__);
		return -ENOMEM;
	}
	rs->pipe = pipe;
	stat = tcp_client_socket_lwip_raw_init(rs, &data->remote_addr,
					       data->port);
	if (stat < 0) {
		printf("%s: error initializing raw socket\n", __func__);
		goto error0;
	}
	pipe->dev_data = bathos_dev_init(dev, &ll_ops, rs);
	if (!pipe->dev_data) {
		stat = -ENODEV;
		goto error0;
	}
	/* FIXME: ALLOCATE CONNECTION BUFFER HERE */
	stat = bathos_dev_open(pipe);
	if (stat < 0)
		goto error1;
	return stat;

error1:
	bathos_dev_uninit(pipe);
error0:
	_client_fini(rs);
	return stat;
}

static int tcp_client_dev_write(struct bathos_pipe *pipe, const char *buf,
				 int len)
{
	struct tcp_client_data *data = pipe->data;
	struct tcp_conn_data *cd = data->impl_priv;

	if (!cd) {
		printf("%s: no connection data\n", __func__);
		return -EINVAL;
	}
	return tcp_client_socket_lwip_raw_send(cd, buf, len);
}

/* No ioctl supported at the moment */
static int tcp_client_dev_ioctl(struct bathos_pipe *pipe,
				 struct bathos_ioctl_data *d)
{
	return -1;
}

static int tcp_client_on_pipe_close(struct bathos_pipe *pipe)
{
	struct tcp_client_data *data = pipe->data;
	struct tcp_conn_data *cd = data->impl_priv;

	bathos_dev_close(pipe);
	bathos_dev_uninit(pipe);

	_client_fini(cd->raw_socket);

	return tcp_client_socket_lwip_raw_fini(cd->raw_socket);
}

const struct bathos_dev_ops tcp_client_dev_ops = {
	.open = tcp_client_dev_open,
	.read = bathos_dev_read,
	.write = tcp_client_dev_write,
	.on_pipe_close = tcp_client_on_pipe_close,
	.ioctl = tcp_client_dev_ioctl,
};
