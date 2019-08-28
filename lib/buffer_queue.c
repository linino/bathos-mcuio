/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Bathos buffer queue implementation */

#include <bathos/buffer_queue_server.h>


int bathos_bqueue_client_init(struct bathos_bqueue *q,
			      const struct event * PROGMEM available,
			      const struct event * PROGMEM processed)
{
	struct bathos_bqueue_data *data = &q->data;

	data->stopped = 1;
	data->available_event = available;
	data->processed_event = processed;
	return 0;
}

struct bathos_bdescr *bathos_bqueue_get_buf_dir(struct bathos_bqueue *q,
						enum buffer_dir dir)
{
	struct bathos_bqueue_data *data = &q->data;
	struct bathos_bdescr *out;
	unsigned long flags;
	struct list_head *free_list;

	if (data->stopped)  {
		printf("%s: queue is stopped\n", __func__);
		return NULL;
	}
	switch (dir) {
	case ANY:
		free_list = &data->free_bufs;
		break;
	case OUT:
		free_list = &data->free_bufs_tx;
		break;
	case IN:
		free_list = &data->free_bufs_rx;
		break;
	default:
		printf("%s: invalid direction %d\n", __func__,
		       __func__, dir);
		return NULL;
	}
	
	interrupt_disable(flags);
	if (list_empty(free_list)) {
		interrupt_restore(flags);
		printf("%s: no free bufs\n", __func__);
		return NULL;
	}
	out = list_first_entry(free_list, struct bathos_bdescr, list);
	/*
	 * [atomically] move buffer to the list of busy buffers
	 */
	list_move(&out->list, &data->busy_bufs);
	interrupt_restore(flags);
	INIT_LIST_HEAD(&out->sglist);
	/*
	 * Trigger a setup event for this queue on next buffer release by
	 * the client
	 */
	bdescr_remap_release_event(out, data->setup_event);
	/* Set buffer's users counter to 1 */
	bdescr_get(out);

	return out;
}

int bathos_bqueue_start(struct bathos_bqueue *q)
{
	struct bathos_bqueue_data *data = &q->data;

	if (data->available_event) {
		pr_debug("%s: triggering available event (%p) for queue %p\n",
			 __func__, data->available_event, q);
		trigger_event(data->available_event, q);
	}
	data->stopped = 0;
	return 0;
}

int bathos_bqueue_stop(struct bathos_bqueue *q)
{
	struct bathos_bqueue_data *data = &q->data;

	data->stopped = 1;
	return 0;
}

int bathos_bqueue_empty(struct bathos_bqueue *q)
{
	struct bathos_bqueue_data *data = &q->data;
	unsigned long flags;
	int ret;

	interrupt_disable(flags);
	ret = list_empty(&data->free_bufs);
	interrupt_restore(flags);
	return ret;
}
