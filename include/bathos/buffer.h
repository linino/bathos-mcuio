#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <stdint.h>
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

enum buffer_dir {
	/* DON'T CARE */
	ANY,
	/* OUT: from cpu to outside */
	OUT,
	/* IN: from outside to cpu */
	IN,
};

struct bathos_sglist_el {
	void				*data;
	unsigned int			len;
	struct list_head		list;
	/*
	 * Element direction: considered for bidirectional transactions only
	 * Must be OUT or IN in such case
	 */
	enum buffer_dir			dir;
};

struct bathos_bdescr {
	enum buffer_dir			dir;
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
	/* User's data pointer, this is untouched by the driver and the pipe */
	void				*user_data;
};

enum bathos_buffer_op_type {
	NONE = 0,
	SEND = 1,
	RECV = 2,
	BIDIR = 3,
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
	/*
	 * spi master, value contains length in bits of cmd, data, mosi and
	 * miso phases
	 */
	SPIM = 4,
	/*
	 * spi slave
	 */
	SPIS = 5,
};

struct bathos_spi_buffer_op_address {
	/* Command */
	uint32_t cmd;
	/* Address */
	uint32_t addr;
	/* Flags */
#define OVERLAPPED_TX_RX
	uint32_t flags;
	/* Length of mosi phase in bits */
	uint16_t mosi_data_bits;
	/* Length of miso phase in bits */
	uint16_t miso_data_bits;
	/* Length of command phase in bits (max 32) */
	uint8_t cmd_bits;
	/* Lenght of address phase in bits (max 32) */
	uint8_t addr_bits;
};

struct bathos_buffer_op_address {
	enum bathos_buffer_op_address_type type;
	unsigned int length;
	union {
		unsigned char b[8];
		struct bathos_spi_buffer_op_address a;
	} val;
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
	if (b->release_event) {
		pr_debug("%s: triggering release event (%p) for buffer %p\n",
			 __func__, b->release_event, b);
		trigger_event(b->release_event, b);
	}
}

static inline struct bathos_bdescr *
bdescr_shallow_copy(struct bathos_bdescr *src)
{
	bdescr_get(src);
	return src;
}

extern struct bathos_bdescr *bdescr_copy(const struct bathos_bdescr *src);


static inline unsigned int bdescr_data_size(struct bathos_bdescr *b)
{
	struct bathos_sglist_el *e;
	unsigned int out = 0;

	if (b->data)
		return b->data_size;
	if (list_empty(&b->sglist))
		return 0;
	list_for_each_entry(e, &b->sglist, list)
		out += e->len;
	return out;
}

static inline void bdescr_remap_release_event(struct bathos_bdescr *b,
					      const struct event *e)
{
	b->release_event = e;
}

static inline int bdescr_add_sglist_el(struct bathos_bdescr *b,
				       struct bathos_sglist_el *e)
{
	list_add_tail(&e->list, &b->sglist);
	return 0;
}


#endif /* __BUFFER_H__ */
