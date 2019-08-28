#ifndef __BUFFER_QUEUE_SERVER_H__
#define __BUFFER_QUEUE_SERVER_H__

#include <bathos/buffer_queue.h>

struct bathos_bqueue_data {
	int stopped;
	const struct event * PROGMEM setup_event;
	const struct event * PROGMEM done_event;
	const struct event * PROGMEM available_event;
	const struct event * PROGMEM processed_event;
	int nbufs;
	struct bathos_buffer_op *op_area;
	struct list_head free_bufs;
	struct list_head busy_bufs;
	struct list_head free_bufs_rx;
	struct list_head free_bufs_tx;
};

struct bathos_bqueue {
	const struct bathos_bqueue_operations *ops;
	struct bathos_bqueue_data data;
};

/*
 * Init server side of the queue. Specify setup and done events, address and
 * size of buffer operations area
 */
extern int bathos_bqueue_server_init_dir(struct bathos_bqueue *q,
					 const struct event * PROGMEM setup,
					 const struct event * PROGMEM done,
					 void *area,
					 int nbufs,
					 int bufsize,
					 enum bathos_buffer_op_address_type
					 atype,
					 enum buffer_dir dir);

static inline
int bathos_bqueue_server_init(struct bathos_bqueue *q,
			      const struct event * PROGMEM setup,
			      const struct event * PROGMEM done,
			      void *area,
			      int nbufs,
			      int bufsize,
			      enum bathos_buffer_op_address_type addr_type)
{
	return bathos_bqueue_server_init_dir(q, setup, done, area, nbufs,
					     bufsize, addr_type, ANY);
}

extern void bathos_bqueue_server_buf_done(struct bathos_bdescr *b);
extern void bathos_bqueue_server_buf_processed(struct bathos_bdescr *b);
extern void
bathos_bqueue_server_buf_processed_immediate(struct bathos_bdescr *b);

/*
 * free queue and relevant buffers
 *
 * Buffers free to be taken by the client are in the free list. Nothing to
 * worry about them.
 * Buffers being setup by the client are in the busy list. The client is
 * responsible for them.
 * The client needs to be notified about buffers being processed by the server
 * while the buffer queue gets finalized. Before calling this function, the
 * server must declare the buffer as processed with -EPIPE.
 *
 */
extern void bathos_bqueue_server_fini(struct bathos_bqueue *);

#endif /* __BUFFER_QUEUE_SERVER_H__ */
