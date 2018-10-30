#ifndef __BUFFER_QUEUE_SERVER_H__
#define __BUFFER_QUEUE_SERVER_H__

#include <bathos/buffer_queue.h>

struct bathos_bqueue_data {
	int stopped;
	const struct event * PROGMEM setup_event;
	const struct event * PROGMEM done_event;
	const struct event * PROGMEM available_event;
	const struct event * PROGMEM processed_event;
	struct list_head free_bufs;
	struct list_head busy_bufs;
};

struct bathos_bqueue {
	const struct bathos_bqueue_operations *ops;
	struct bathos_bqueue_data data;
};

/*
 * Init server side of the queue. Specify setup and done events, address and
 * size of buffer operations area
 */
int bathos_bqueue_server_init(struct bathos_bqueue *,
			      const struct event * PROGMEM setup,
			      const struct event * PROGMEM done,
			      void *area,
			      int nbufs,
			      int bufsize,
			      enum bathos_buffer_op_address_type addr_type);

extern void bathos_bqueue_server_buf_done(struct bathos_bdescr *b);
extern void bathos_bqueue_server_buf_processed(struct bathos_bdescr *b);

#endif /* __BUFFER_QUEUE_SERVER_H__ */
