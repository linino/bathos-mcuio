/*
 * Command line handling functions
 *
 * Author Davide Ciminaghi <ciminaghi@gnudd.com> 2017
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <bathos/cmdline.h>

struct cmdline_item *_find_item(const char *n)
{
	struct cmdline_item *item;

	for (item = cmdline_items_start; item != cmdline_items_end; item++)
		if (!strncmp(item->name, n, strlen(item->name)))
			return item;
	return NULL;
}

int parse_cmdline(int argc, char *argv[])
{
	int i;
	struct cmdline_item *item;

	for (i = 0; i < argc; i++) {
		item = _find_item(argv[i]);
		if (item)
			if (item->parse(argv[i], item->data) < 0)
				return -1;
	}
	return 0;
}
