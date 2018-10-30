#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <bathos/event.h>
#include <linux/list.h>

/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Generic buffer [for async operations] */
/*
 * The following events can be emitted by a buffer:
 *
 * release: this is emitted when the buffer's users counter drops to zero
 */

struct bathos_bqueue;

struct bathos_sglist_el {
	void				*data;
	int				len;
	struct list_head		list;
};

struct bathos_bdescr {
	/*
	 * Either we have data != NULL && sglist empty or data == NULL
	 * and sglist !empty
	 */
	void				*data;
	unsigned int			data_size;
	struct list_head		sglist;
	int				error;
	int				users;
	const struct event		*release_event;
	struct bathos_bqueue		*queue;
	struct list_head		list;
};

enum bathos_buffer_op_type {
	NONE = 0,
	SEND = 1,
	RECV = 2,
};

enum bathos_buffer_op_address_type {
	/* we don't care about the address */
	DONTCARE = 0,
	/* local copy, for instance DMA, fixed destination or source */
	LOCAL_MEMORY_NOINC = 1,
	/* local copy, for instance DMA, src or dst address to be incremented */
	LOCAL_MEMORY_INC = 2,
	/* send to/recv from remote mac address */
	REMOTE_MAC = 3,
};

struct bathos_buffer_op_address {
	enum bathos_buffer_op_address_type type;
	unsigned int length;
	unsigned char val[8];
};

/*
 * This describes an operation on a bathos buffer. Note that the buffer is
 * part of the structure, so we can derive a pointer to the operation given
 * the address of its operand (via container_of).
 */
struct bathos_buffer_op {
	enum bathos_buffer_op_type type;
	struct bathos_buffer_op_address addr;
	struct bathos_bdescr operand;
};

#define to_operation(b) container_of(b, struct bathos_buffer_op, operand)

static inline int bdescr_error(struct bathos_bdescr *b)
{
	return b->error;
}

static inline void bdescr_set_error(struct bathos_bdescr *b, int e)
{
	b->error = e;
}

static inline void bdescr_get(struct bathos_bdescr *b)
{
	unsigned long flags;

	interrupt_disable(flags);
	b->users++;
	interrupt_restore(flags);
}

static inline void bdescr_put(struct bathos_bdescr *b)
{
	unsigned long flags;

	interrupt_disable(flags);
	if (--b->users) {
		interrupt_restore(flags);
		return;
	}
	interrupt_restore(flags);
	if (b->release_event)
		trigger_event(b->release_event, b);
}

static inline struct bathos_bdescr *
bdescr_shallow_copy(struct bathos_bdescr *src)
{
	bdescr_get(src);
	return src;
}

extern struct bathos_bdescr *bdescr_copy(const struct bathos_bdescr *src);


static inline void *bdescr_data(struct bathos_bdescr *b,
				unsigned int *data_size)
{
	*data_size = b->data_size;
	return b->data;
}

static inline void bdescr_remap_release_event(struct bathos_bdescr *b,
					      const struct event *e)
{
	b->release_event = e;
}


#endif /* __BUFFER_H__ */
