/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Bathos buffer queue implementation */

#include <bathos/buffer_queue_server.h>
#include <bathos/allocator.h>
#include <bathos/string.h>


int bathos_bqueue_server_init(struct bathos_bqueue *q,
			      const struct event * PROGMEM setup,
			      const struct event * PROGMEM done,
			      void *area,
			      int nbufs,
			      int bufsize,
			      enum bathos_buffer_op_address_type addr_type)
{
	struct bathos_bqueue_data *data = &q->data;
	struct bathos_buffer_op *op, *op_area;
	void *data_ptr;
	int i;

	data->setup_event = setup;
	data->done_event = done;
	INIT_LIST_HEAD(&data->busy_bufs);
	INIT_LIST_HEAD(&data->free_bufs);
	if (!nbufs)
		return 0;
	if (nbufs < 0) {
		printf("ERR: %s, invalid buf number %d\n", __func__, nbufs);
		return -EINVAL;
	}
	op_area = bathos_alloc_buffer(sizeof(*op) * nbufs);
	if (!op_area) {
		printf("ERR: %s, not enough memory for buffer queue\n",
		       __func__);
		return -ENOMEM;
	}
	for (i = 0, op = op_area, data_ptr = area ? area : NULL; i < nbufs;
	     i++, op++, data_ptr = area ? data_ptr + bufsize : NULL) {
		op->type = NONE;
		op->addr.type = addr_type;
		op->operand.data = data_ptr;
		op->operand.queue = q;
		memset(op->addr.val.b, 0xff, sizeof(op->addr.val));
		list_add(&op->operand.list, &data->free_bufs);
		printf("added %p to free buf list %p\n",
		       &op->operand.list, &data->free_bufs);
	}
	return 0;
}

void bathos_bqueue_server_buf_done(struct bathos_bdescr *b)
{
	struct bathos_bqueue *q = b->queue;
	struct bathos_bqueue_data *data = &q->data;
	unsigned long flags;
	int free_list_was_empty = 0;

	/* Atomically move buffer to free list */
	interrupt_disable(flags);
	if (list_empty(&data->free_bufs))
		free_list_was_empty = 1;
	list_move(&b->list, &data->free_bufs);
	interrupt_restore(flags);
	if (data->available_event && free_list_was_empty)
		trigger_event(data->available_event, q);
}

void bathos_bqueue_server_buf_processed(struct bathos_bdescr *b)
{
	struct bathos_bqueue *q = b->queue;
	struct bathos_bqueue_data *data = &q->data;

	trigger_event(data->processed_event, b);
}

void bathos_bqueue_server_buf_processed_immediate(struct bathos_bdescr *b)
{
	struct bathos_bqueue *q = b->queue;
	struct bathos_bqueue_data *data = &q->data;

	trigger_event_immediate(data->processed_event, b);
}
