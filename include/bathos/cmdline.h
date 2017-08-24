/*
 * Bathos command line functions and declarations
 *
 * Author Davide Ciminaghi <ciminaghi@gnudd.com> 2017
 */
#ifndef __BATHOS_CMDLINE_H__
#define __BATHOS_CMDLINE_H__

struct cmdline_item {
	const char *name;
	int (*parse)(const char *, void *);
	void *data;
	int dummy;
};

#define declare_cmdline_handler(n,p,d)				\
	const struct cmdline_item cmdline_item_ ## n		\
	__attribute__((section(".cmdline_items"), aligned(32))) = {	\
		.name = #n,					\
		.parse = p,					\
		.data = d,					\
	};


extern struct cmdline_item cmdline_items_start[], cmdline_items_end[];

extern int parse_cmdline(int argc, char *argv[]);

#endif /* __BATHOS_CMDLINE_H__ */

