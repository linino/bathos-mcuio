/*
 * Unix low level eth driver, uses packet socket (see man 7 packet).
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <endian.h>
#include <unistd.h>
#include <libudev.h>
#include <linux/if_packet.h>
#include <linux/if.h>
#include <net/ethernet.h> /* ETHER_ADDR_LEN */
#include <stdint.h>
#include <errno.h>
#include <bathos/string.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/buffer_queue_server.h>
#include <bathos/unix-eth.h>
#include <arch/pipe.h>

struct unix_eth_priv {
	struct bathos_pipe *pipe;
	struct list_head rx_queue;
	struct list_head tx_queue;
	void *buffer_area;
	struct bathos_dev_data *dev_data;
	/* Socket file descriptor */
	int fd;
	int if_index;
};

declare_event(unix_eth_setup);
declare_event(unix_eth_done);
declare_event(unix_eth_input);

static void start_tx(struct unix_eth_priv *priv)
{
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	struct sockaddr_ll addr;
	const struct unix_eth_platform_data *pdata =
		priv->pipe->dev->platform_data;

	if (list_empty(&priv->tx_queue))
		/* Nothing in list, should not happen */
		return;
	b = list_first_entry(&priv->tx_queue, struct bathos_bdescr, list);
	op = to_operation(b);
	/* Send packet here */
	/* Only accept eth type packets */
	if (op->addr.type != REMOTE_MAC) {
		bathos_bqueue_server_buf_done(b);
		return;
	}
	if (op->addr.length != ETHER_ADDR_LEN) {
		bathos_bqueue_server_buf_done(b);
		return;
	}
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(pdata->eth_type);
	addr.sll_ifindex = priv->if_index;
	addr.sll_halen = ETHER_ADDR_LEN;
	memcpy(addr.sll_addr, op->addr.val.b, ETHER_ADDR_LEN);
	if (sendto(priv->fd, b->data, b->data_size, 0,
		   (struct sockaddr *)&addr, sizeof(addr)) < 0)
		printf("sendto error\n");
	
	/* FIXME: can buffer be released here ? */
	bathos_bqueue_server_buf_done(b);
}

/*
 * This is invoked when a buffer has been setup by the client and is
 * ready for tx/rx submission
 */
static void unix_eth_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;
	struct bathos_bqueue *q;
	struct unix_eth_priv *priv;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	op = to_operation(b);
	q = b->queue;
	printf("%s %d, queue = %p\n", __func__, __LINE__, q);
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
declare_event_handler(unix_eth_setup, NULL, unix_eth_setup_handler, NULL);


/*
 * This is invoked when a buffer has been released by the client
 */
static void unix_eth_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(unix_eth_done, NULL, unix_eth_done_handler, NULL);

/*
 * This is invoked when a packet is coming from the socket
 */
static void unix_eth_input_ready_handler(struct event_handler_data *ed)
{
	struct bathos_pipe *p = ed->data;
	struct bathos_dev *dev = p->dev;
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(p);
	struct unix_eth_priv *priv = bathos_bqueue_to_ll_priv(bq);
	struct arch_unix_pipe_data *adata = dev->arch_priv;
	const struct unix_eth_platform_data *pdata = dev->platform_data;
	struct bathos_bdescr *b;
	struct bathos_buffer_op *op;
	int stat;
	
	printf("%s\n", __func__);
	if (list_empty(&priv->rx_queue)) {
		/* Discard packet: FIXME: IS THIS OK ? */
		char buf[4];
		printf("%s: overrun error\n", __func__);
		(void)recv(adata->fd, buf, sizeof(buf), 0);
		return;
	}
	/* Read data and enqueue buffer for processing */
	b = list_first_entry(&priv->rx_queue, struct bathos_bdescr, list);
	stat = recv(adata->fd, b->data, pdata->bufsize, 0);
	if (stat < 0) {
		printf("%s: rx error\n", __func__);
		return;
	}
	b->data_size = stat;
	op = to_operation(b);
	printf("%s, b = %p, op->type = %d\n", __func__, b, op->type);
	bathos_bqueue_server_buf_processed(b);
}
declare_event_handler(unix_eth_input, NULL, unix_eth_input_ready_handler, NULL);

static int eth_socket(struct unix_eth_priv *priv,
		      const struct unix_eth_platform_data *pdata)
{
	struct ifreq ifr;
	size_t if_name_len = strlen(pdata->ifname);
	struct sockaddr_ll addr;

	priv->fd = socket(AF_PACKET, SOCK_DGRAM, htons(pdata->eth_type));
	if (priv->fd < 0) {
		printf("socket: error (%d)\n", errno);
		return priv->fd;
	}
	memcpy(ifr.ifr_name, pdata->ifname, if_name_len);
	ifr.ifr_name[if_name_len] = 0;
	if (ioctl(priv->fd, SIOCGIFINDEX, &ifr) == -1) {
		printf("%s, ioctl, SIOCGIFINDEX: %d",
		       __func__, errno);
		close(priv->fd);
		return -1;
	}
	priv->if_index = ifr.ifr_ifindex;
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = ifr.ifr_ifindex;
	addr.sll_protocol = htons(pdata->eth_type);
	if (bind(priv->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("%s, bind: %d\n", __func__, errno);
		close(priv->fd);
		return -1;
	}
	return 0;
}

static int unix_eth_async_open(void *_priv)
{
	int ret;
	struct unix_eth_priv *priv = _priv;
	struct bathos_bqueue *q = bathos_dev_get_bqueue(priv->pipe);
	const struct unix_eth_platform_data *pdata =
		priv->pipe->dev->platform_data;
	struct arch_unix_pipe_data *adata = priv->pipe->dev->arch_priv;

	printf("%s %d, queue = %p, priv = %p\n", __func__, __LINE__, q, priv);
	if (!pdata) {
		printf("%s: no platform data\n", __func__);
		return -1;
	}
	
	ret = bathos_bqueue_server_init(q,
					&event_name(unix_eth_setup),
					&event_name(unix_eth_done),
					priv->buffer_area,
					pdata->nbufs,
					pdata->bufsize,
					NONE);
	if (ret)
		return ret;

	/* Open and init socket here */
	if (eth_socket(priv, pdata) < 0)
		return -1;
	adata->fd = priv->fd;
	adata->pipe = priv->pipe;
	pipe_remap_input_ready_event(priv->pipe,
				     &event_name(unix_eth_input));
	return 0;
}

static const struct bathos_ll_dev_ops unix_eth_ll_dev_ops = {
	.async_open = unix_eth_async_open,
};


static int unix_eth_open(struct bathos_pipe *pipe)
{
	struct unix_eth_priv *priv;
	struct bathos_dev *dev = pipe->dev;
	const struct unix_eth_platform_data *pdata = dev->platform_data;
	struct arch_unix_pipe_data *adata;
	int ret;

	if (!pipe_mode_is_async(pipe))
		return -EINVAL;

	if (!pdata)
		return -ENODEV;

	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	adata = unix_get_pipe_arch_data();
	if (!adata) {
		ret = -ENOMEM;
		goto error0;
	}

	priv->pipe = pipe;
	INIT_LIST_HEAD(&priv->rx_queue);
	INIT_LIST_HEAD(&priv->tx_queue);
	priv->buffer_area = bathos_alloc_buffer(pdata->nbufs * pdata->bufsize);
	if (!priv->buffer_area) {
		ret = -ENOMEM;
		goto error1;
	}
	pipe->dev_data = bathos_dev_init(&unix_eth_ll_dev_ops, priv);
	if (!pipe->dev_data) {
		ret = -ENOMEM;
		goto error2;
	}
	dev->arch_priv = adata;
	printf("%s %d, dev->priv = %p\n", __func__, __LINE__, dev->priv);
	return bathos_dev_open(pipe);

error2:
	bathos_free_buffer(priv->buffer_area, pdata->nbufs * pdata->bufsize);
error1:
	unix_free_pipe_arch_data(adata);
error0:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}


const struct bathos_dev_ops unix_eth_dev_ops = {
	.open = unix_eth_open,
	.get_bqueue = bathos_dev_get_bqueue,
};
