#ifndef __MACH_EAGLE_DELAY_H__
#define __MACH_EAGLE_DELAY_H__

#include <stdint.h>
#include <bathos/jiffies.h>
#include <bathos/errno.h>

#define time_after_ll(a,b)			\
    ((long long)(b) - (long long)(a) < 0)

#define time_before_ll(a,b) time_after_ll(b,a)

/* from https://sub.nanona.fi/esp8266/timing-and-ticks.html */
static inline int32_t __asm_ccount(void)
{
	int32_t r;

	asm volatile ("rsr %0, ccount" : "=r"(r));
	return r;
}

/* 80 MHz is standard, could be 160, fix this later on */
#define CCOUNTS_PER_SEC 80000000ULL

static inline int __arch_ndelay(int n)
{
	unsigned long long start = __asm_ccount(), end;

	/* max 0.5 sec */
	if (n >= 500000000)
		return -EINVAL;

	end = start + ((n * CCOUNTS_PER_SEC)/1000000000ULL);

	while (time_before_ll(__asm_ccount(), end));
	return 0;
}

static inline int __arch_udelay(int u)
{
	return __arch_ndelay(u * 1000);
}

#endif
