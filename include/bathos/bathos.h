/*
 * Main header for Bathos
 *  Alessandro Rubini, 2009-2012 GNU GPL2 or later
 */
#ifndef __BATHOS_H__
#define __BATHOS_H__

#ifndef PROGMEM
#define PROGMEM
#endif

/*
 * stdio-related stuff is related to bathos/stdio.h, but users are not
 * expected to include it (because it sounds very ugly)
 */
#include <bathos/stdio.h>

/* Ever-needed definitions */
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define ALIGN(a,b) (((a) + ((b) - 1)) & ((b) - 1))
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define cat(a,b) a##b
#define xcat(a,b) cat(a,b)
#define str(a) #a
#define xstr(a) str(a)

/* Other misc bathos stuff */
extern int bathos_main(void);
extern int bathos_setup(void);

/*
 * This function is implemented as a weak symbol in main.c.
 * You're free to override the default implementation in case the default
 * main loop is not suitable for your particular platform.
 */
extern void bathos_loop(void);

/* And finally the task definition */
struct bathos_task {
	char *name;
	void *(*job)(void *);
	int (*init)(void *);
	void *arg;
	unsigned long period;
	unsigned long release;
};

#define __task __attribute__((section(".task"),__used__))

extern struct bathos_task __task_begin[], __task_end[];

#endif /* __BATHOS_H__ */
