/*
 * Copyright (c) 2018 dog hunter LLC and the Linino community
 * Author: Davide Ciminaghi <davide@linino.org> 2018
 *
 * Linino.org is a dog hunter sponsored community project
 *
 * General Public License version 2 (GPLv2)
 */
#include <bathos/buffer.h>

extern int bdescr_copy_lin(struct bathos_bdescr *b, void *ptr)
{
	int copied = 0;

	struct bathos_sglist_el *e;

	if (b->data) {
		memcpy(ptr, b->data, b->data_size);
		return b->data_size;
	}

	list_for_each_entry(e, &b->sglist, list) {
		memcpy(ptr + copied, e->data, e->len);
		copied += e->len;
	}
	return copied;
}
