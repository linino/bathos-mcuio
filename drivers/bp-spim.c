/*
 * Use the bus pirate as an spi master under bathos (arch-unix).
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/bp-spim.h>
#include <bathos/buffer_queue_server.h>

struct bp_spim_priv {
	const struct bp_spim_platform_data *plat;
	struct list_head queue;
	void *buffer_area;
	struct bathos_bdescr *b;
	struct bathos_sglist_el *tx_el;
	struct bathos_sglist_el *rx_el;
};

declare_event(bp_spim_setup);
declare_event(bp_spim_done);

static int bp_spim_async_open(void *_priv)
{
	return 0;
}

static const struct bathos_ll_dev_ops bp_spim_ll_dev_ops = {
	.async_open = bp_spim_async_open,
};


static int bp_spim_open(struct bathos_pipe *pipe)
{
	struct bp_spim_priv *priv;
	struct bathos_dev *dev = pipe->dev;
	const struct bp_spim_platform_data *plat = dev->platform_data;
	int ret;

	if (!pipe_mode_is_async(pipe))
		return -EINVAL;

	priv = bathos_alloc_buffer(sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->plat = plat;
	INIT_LIST_HEAD(&priv->queue);
	INIT_LIST_HEAD(&priv->queue);
	priv->buffer_area = bathos_alloc_buffer(plat->nbufs * plat->bufsize);
	if (!priv->buffer_area) {
		ret = -ENOMEM;
		goto error0;
	}
	pipe->dev_data = bathos_dev_init(dev, &bp_spim_ll_dev_ops,
					 priv);
	if (!pipe->dev_data) {
		ret = -ENOMEM;
		goto error1;
	}
	ret = bathos_dev_open(pipe);
	if (ret < 0)
		goto error2;
	if (pipe->mode & BATHOS_MODE_ASYNC) {
		struct bathos_bqueue *q = dev->ops->get_bqueue ?
			dev->ops->get_bqueue(pipe) : NULL;

		if (!q)
			goto error2;
		ret = bathos_bqueue_server_init(q,
						&event_name(bp_spim_setup),
						&event_name(bp_spim_done),
						priv->buffer_area,
						plat->nbufs,
						plat->bufsize,
						SPIM);
	}
	if (ret)
		goto error2;
	return ret;

error2:
	bathos_dev_uninit(pipe);
error1:
	bathos_free_buffer(priv->buffer_area, plat->nbufs * plat->bufsize);
error0:
	bathos_free_buffer(priv, sizeof(*priv));
	return ret;
}

static int bp_spim_close(struct bathos_pipe *pipe)
{
	struct bathos_bqueue *bq = bathos_dev_get_bqueue(pipe);
	struct bp_spim_priv *priv = bathos_bqueue_to_ll_priv(bq);
	struct bathos_dev *dev = pipe->dev;
	const struct bp_spim_platform_data *plat = dev->platform_data;
	struct bathos_bdescr *b, *tmp;

	/* Tell the client we're closing */
	list_for_each_entry_safe(b, tmp, &priv->queue, list) {
		/* buffer queue is being killed */
		b->error = -EPIPE;
		bathos_bqueue_server_buf_processed_immediate(b);
	}
	/* Stop and finalize the queue */
	bathos_bqueue_stop(bq);
	bathos_bqueue_server_fini(bq);
	/* Free the remaining resources */
	bathos_dev_close(pipe);
	bathos_dev_uninit(pipe);
	bathos_free_buffer(priv->buffer_area, plat->nbufs * plat->bufsize);
	bathos_free_buffer(priv, sizeof(*priv));
	return 0;
}


const struct bathos_dev_ops bp_spim_dev_ops = {
	.open = bp_spim_open,
	.get_bqueue = bathos_dev_get_bqueue,
	.close = bp_spim_close,
};
