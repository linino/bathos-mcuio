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

struct bathos_bdescr *bathos_bqueue_get_buf(struct bathos_bqueue *q)
{
	struct bathos_bqueue_data *data = &q->data;
	struct bathos_bdescr *out;
	unsigned long flags;

	if (data->stopped)  {
		printf("%s: queue is stopped\n", __func__);
		return NULL;
	}
	interrupt_disable(flags);
	if (list_empty(&data->free_bufs)) {
		interrupt_restore(flags);
		printf("%s: no free bufs\n", __func__);
		return NULL;
	}
	out = list_first_entry(&data->free_bufs, struct bathos_bdescr, list);
	/*
	 * [atomically] move buffer to the list of busy buffers
	 */
	list_move(&out->list, &data->busy_bufs);
	interrupt_restore(flags);
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

	if (data->available_event)
		trigger_event(data->available_event, q);
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

	return list_empty(&data->free_bufs);
}
