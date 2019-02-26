#ifndef __JIFFIES_H__
#define __JIFFIES_H__
#include <arch/bathos-arch.h>
/*
 * The architecture may define __get_jiffies if the hardware doesn't
 * provide a single 32-bit increasing counter at some address
 */
#ifdef __get_jiffies
#  define jiffies __get_jiffies()
#else
  extern volatile unsigned long jiffies;
#endif

#ifndef time_delta
#define time_delta(a,b)				\
	((long)b - (long)a)
#endif

/* The following ones come from the kernel, but simplified */
#ifndef time_after
#define time_after(a,b)		\
	((long)(b) - (long)(a) < 0)
#endif
#define time_before(a,b)	time_after(b,a)
#ifndef time_after_eq
#define time_after_eq(a,b)	\
	 ((long)(a) - (long)(b) >= 0)
#endif
#define time_before_eq(a,b)	time_after_eq(b,a)

#define time_in_range(a,b,c) \
	(time_after_eq(a,b) && \
	 time_before_eq(a,c))

#endif /* __JIFFIES_H__ */
