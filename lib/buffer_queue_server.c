/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Bathos buffer queue implementation */

#include <bathos/buffer_queue_server.h>
#include <bathos/allocator.h>
#include <bathos/string.h>


int bathos_bqueue_server_init_dir(struct bathos_bqueue *q,
				  const struct event * PROGMEM setup,
				  const struct event * PROGMEM done,
				  void *area,
				  int nbufs,
				  int bufsize,
				  enum bathos_buffer_op_address_type addr_type,
				  enum buffer_dir dir)
{
	struct bathos_bqueue_data *data = &q->data;
	struct bathos_buffer_op *op, *op_area;
	void *data_ptr;
	int i;
	struct list_head *list = NULL;

	data->setup_event = setup;
	data->done_event = done;
	data->nbufs = nbufs;

	switch (dir) {
	case ANY:
		list = &data->free_bufs;
		break;
	case OUT:
		list = &data->free_bufs_tx;
		break;
	case IN:
		list = &data->free_bufs_rx;
		break;
	default:
		printf("ERR: %s, invalid direction %d\n", __func__, dir);
		return -EINVAL;
	}
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
	data->op_area = op_area;
	for (i = 0, op = op_area, data_ptr = area ? area : NULL; i < nbufs;
	     i++, op++, data_ptr = area ? data_ptr + bufsize : NULL) {
		op->type = NONE;
		op->addr.type = addr_type;
		op->operand.dir = dir;
		op->operand.data = data_ptr;
		op->operand.queue = q;
		memset(op->addr.val.b, 0xff, sizeof(op->addr.val));
		INIT_LIST_HEAD(&op->operand.sglist);
		list_add(&op->operand.list, list);
		printf("added %p (dir = %d) to buf list %p\n",
		       &op->operand.list, op->operand.dir, list);
	}
	return 0;
}

void bathos_bqueue_server_buf_done(struct bathos_bdescr *b)
{
	struct bathos_bqueue *q = b->queue;
	struct bathos_bqueue_data *data = &q->data;
	unsigned long flags;
	int free_list_was_empty = 0;
	struct list_head *free_list;

	switch (b->dir) {
	case ANY:
		free_list = &data->free_bufs;
		pr_debug("%s: Buf %p back to free list\n", __func__, b);
		break;
	case OUT:
		free_list = &data->free_bufs_tx;
		pr_debug("%s: Buf %p back to tx free list\n", __func__, b);
		break;
	case IN:
		free_list = &data->free_bufs_rx;
		pr_debug("%s: Buf %p back to rx free list\n", __func__, b);
		break;
	default:
		printf("%s: buffer %p has invalid direction %d\n", __func__,
		       b, b->dir);
		return;
	}

	/* Atomically move buffer to free list */
	interrupt_disable(flags);
	if (list_empty(free_list))
		free_list_was_empty = 1;
	if (!list_empty(&b->list))
		list_move(&b->list, free_list);
	else
		list_add(&b->list, free_list);
	interrupt_restore(flags);
	if (data->available_event && free_list_was_empty) {
		pr_debug("%s: triggering available event (%p)\n", __func__,
			 data->available_event);
		trigger_event(data->available_event, q);
	}
}

void bathos_bqueue_server_buf_processed(struct bathos_bdescr *b)
{
	struct bathos_bqueue *q = b->queue;
	struct bathos_bqueue_data *data = &q->data;

	pr_debug("%s: triggering processed event (%p) for buffer %p\n",
		 __func__, data->processed_event, b);
	bdescr_remap_release_event(b, data->done_event);
	bdescr_get(b);
	if (b->users != 1) {
		printf("%s: bdescr users = %d\n", __func__, b->users);
		return;
	}
	trigger_event(data->processed_event, b);
}

void bathos_bqueue_server_buf_processed_immediate(struct bathos_bdescr *b)
{
	struct bathos_bqueue *q = b->queue;
	struct bathos_bqueue_data *data = &q->data;

	trigger_event_immediate(data->processed_event, b);
}


void bathos_bqueue_server_fini(struct bathos_bqueue *q)
{
	struct bathos_bqueue_data *data = &q->data;

	bathos_free_buffer(data->op_area,
			   data->nbufs * sizeof(*(data->op_area)));
}
