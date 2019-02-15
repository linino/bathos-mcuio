/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */


#ifndef __BATHOS_ARCH_H__
#define __BATHOS_ARCH_H__

#define ARCH_NEEDS_INTERRUPTS_FOR_JIFFIES 1
#define ARCH_IS_HARVARD 1

#include <stddef.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#define interrupt_disable(flags) \
    do {			 \
        flags = SREG;		 \
	cli();			 \
    } while(0)

#define interrupt_restore(flags) \
    SREG = flags

#define time_after(a,b)		\
    ((int)(b) - (int)(a) < 0)

#define time_after_eq(a,b)	\
    (((int)(a) - (int)(b)) >= 0)

static inline int __copy_byte(uint8_t *dst, const uint8_t *src)
{
	*dst = pgm_read_byte_near(src);
	return 0;
}

#define __copy_char(a, b) __copy_byte((uint8_t *)a, (uint8_t *)b)

static inline int __copy_word(uint16_t *dst, const uint16_t *src)
{
	*dst = pgm_read_word_near(src);
	return 0;
}

#define __copy_int(a, b) __copy_word((uint16_t *)a, (uint16_t *)b)

static inline int __copy_dword(uint32_t *dst, const uint32_t *src)
{
	*dst = pgm_read_dword_near(src);
	return 0;
}

#define __copy_long(a, b) __copy_dword((unit32_t *)a, (uint32_t *)b)
#define __copy_ulong(a, b) __copy_dword(a, b)

static inline void *memcpy_p(void *dst, const void *src, int size)
{
	return memcpy_P(dst, src, size);
}

static inline void flip4(uint8_t *x)
{
	uint8_t t;
	t = x[0]; x[0] = x[3]; x[3] = t;
	t = x[1]; x[1] = x[2]; x[2] = t;
}

#endif /* __BATHOS_ARCH_H__ */
