/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* lininoio internal header file */
#ifndef __LININOIO_INTERNAL_H__
#define __LININOIO_INTERNAL_H__

#include <stdint.h>
#include <lininoio.h>
#include <bathos/bathos.h>
#include <bathos/event.h>

declare_extern_event(lininoio_buf_available);
declare_extern_event(lininoio_buf_processed);

struct bathos_pipe;

struct lininoio_channel_data {
	struct bathos_pipe *io_pipe;
	void *priv;
};

struct lininoio_channel_descr {
	uint16_t contents_id;
};

struct lininoio_channel;

struct lininoio_channel_ops {
	int (*init)(const struct lininoio_channel *, struct bathos_pipe *);
	int (*setup)(const struct lininoio_channel *,
		     struct lininoio_association_data *adata);
	int (*input)(const struct lininoio_channel *, void *, uint16_t);
};

struct lininoio_channel {
	const struct lininoio_channel_descr * PROGMEM descr;
	const struct lininoio_channel_ops * PROGMEM ops;
	struct lininoio_channel_data *data;
};

#define __lininoio_chan __attribute__((section(".lininoio_channels"), used))

#define declare_lininoio_channel(n,d,o,da)		\
    struct lininoio_channel n __lininoio_chan = {	\
	    .descr = d,					\
	    .ops = o,					\
	    .data = da,					\
    }

extern struct lininoio_channel lininoio_channels_start[],
	lininoio_channels_end[];

#endif /* __LININOIO_INTERNAL_H__ */
