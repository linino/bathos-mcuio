#ifndef __BUFFER_QUEUE_H__
#define __BUFFER_QUEUE_H__

#include <bathos/event.h>
#include <bathos/buffer.h>

/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Buffer queue header file, client interface */
/*
 * Buffer queue events:
 *
 *    CLIENT SIDE        |         SERVER SIDE
 *                       |
 *                <---- available ----
 *        setup buffer   |
 *                ----- setup --->
 *                       |         process buffer
 *                <---- processed ---
 *      read data/error  |
 *                 ---- done ---->
 *                       |         reenqueue buffer in free queue and
 *                       |         start again
 *
 * 1) Buffers are allocated by the server. An "available" event is sent to
 *    the client to tell it that at least a buffer is ready for setup.
 * 2) Client receives the available event, gets a buffer, sets it up and
 *    releases it, thus triggering a "setup" event.
 * 3) Server receives the setup event, processes the buffer and sends the
 *    client a "processed" event.
 * 4) Client receives the ready event, gets data out of the buffer
 *    (if necessary) and releases it.
 *
 * The available event is optional. In case it is NULL, the client is
 * responsible of polling the queue for free buffers.
 * The processed event is also optional (think for instance of a tx queue: the
 * clients sets up a buffer and the server sends it. There might be no need
 * for the client to be notified about tx finished). In case no "processed"
 * event is specified by the client, the buffer is immediately moved on to
 * the free queue and an "available" event is triggered.
 */

/* This is an opaque data type for the client */
struct bathos_bqueue;

/*
 * Init client side of the queue. Specify available and processed events
 */
int bathos_bqueue_client_init(struct bathos_bqueue *,
			      const struct event * PROGMEM available,
			      const struct event * PROGMEM processed);

/*
 * Get a buffer (used by the client in polling mode)
 */
struct bathos_bdescr *bathos_bqueue_get_buf(struct bathos_bqueue *);

/*
 * Start buffer queue: this will result in an available event, so redirect
 * the available event __before__ starting the queue
 */
int bathos_bqueue_start(struct bathos_bqueue *);

/* Stop the buffer queue */
int bathos_bqueue_stop(struct bathos_bqueue *);

/* Return !0 when queue is empty */
int bathos_bqueue_empty(struct bathos_bqueue *);

#endif /* __BUFFER_QUEUE_H__ */
