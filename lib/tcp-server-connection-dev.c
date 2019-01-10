/*
 * tcp/ip server connection implementation
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

/* Only write supported at the moment, no ioctl */
static const struct bathos_ll_dev_ops ll_ops = {
};

static int tcp_server_connection_dev_open(struct bathos_pipe *pipe)
{
	struct bathos_dev *dev = pipe->dev;

	pipe->dev_data = bathos_dev_init(dev, &ll_ops, pipe->data);
	if (!pipe->dev_data)
		return -ENODEV;
	/* FIXME: ALLOCATE CONNECTION BUFFER HERE */
	return bathos_dev_open(pipe);
}

int tcp_server_connection_dev_write(struct bathos_pipe *pipe, const char *buf,
				    int len)
{
	struct tcp_conn_data *cd = pipe->data;

	if (!cd) {
		printf("%s: no connection data\n", __func__);
		return -EINVAL;
	}
	return tcp_server_socket_lwip_raw_send(cd, buf, len);
}

int tcp_server_connection_dev_close(struct bathos_pipe *pipe)
{
	int stat;
	struct tcp_conn_data *cd = pipe->data;

	if (!cd) {
		printf("%s: no connection data\n", __func__);
		return -EINVAL;
	}
	printf("%s: pipe = %p, cd = %p\n", __func__, pipe, cd);
	stat = tcp_server_socket_lwip_raw_close(cd);
	if (stat < 0) {
		printf("%s: could not close\n", __func__);
		return -EINVAL;
	}
	cd->can_close = 1;
	bathos_dev_uninit(pipe);
	return stat;
}

const struct bathos_dev_ops tcp_server_connection_dev_ops = {
	.open = tcp_server_connection_dev_open,
	.read = bathos_dev_read,
	.write = tcp_server_connection_dev_write,
	.on_pipe_close = tcp_server_connection_dev_close,
	.ioctl = bathos_dev_ioctl,
};

