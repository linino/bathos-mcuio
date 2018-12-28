/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef _ASM_GENERIC_BITOPS_NON_ATOMIC_H_
#define _ASM_GENERIC_BITOPS_NON_ATOMIC_H_

//#include <asm/types.h>
#include <bathos/interrupt.h>

#ifndef BITS_PER_LONG
#define BITS_PER_LONG		32
#endif

#ifndef BITS_PER_LONG_LONG
#define BITS_PER_LONG_LONG	64
#endif

#ifndef BITS_PER_BYTE
#define BITS_PER_BYTE		8
#endif

#if (BITS_PER_BYTE != 8)
# error "UNSUPPORTED BITS_PER_BYTE"
#else
# define BITS_PER_BYTE_LOG2 3
#endif

#if (BITS_PER_LONG == 32)
# define BITS_PER_LONG_LOG2 5
#elif (BITS_PER_LONG == 64)
# define BITS_PER_LONG_LOG2 6
#else
# error "UNSUPPORTED BITS_PER_LONG"
#endif

#if (BITS_PER_LONG_LONG == 64)
# define BITS_PER_LONG_LONG_LOG2 6
#else
# error "UNSUPPORTED BITS_PER_LONG_LONG"
#endif

#define BIT(nr)			(1UL << (nr))
#define BIT_ULL(nr)		(1ULL << (nr))
#define BIT_MASK(nr)		(1UL << ((nr) & (BITS_PER_LONG - 1)))
#define BIT_WORD(nr)		((nr) >> BITS_PER_LONG_LOG2)
#define BIT_ULL_MASK(nr)	(1ULL << ((nr) & (BITS_PER_LONG_LONG - 1)))
#define BIT_ULL_WORD(nr)	((nr) >> BITS_PER_LONG_LONG_LOG2)
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, \
					     sizeof(long) << BITS_PER_BYTE_LOG2)

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static inline void __set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long flags;

	interrupt_disable(flags);
	__set_bit(nr, addr);
	interrupt_restore(flags);
}

static inline void __clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long flags;

	interrupt_disable(flags);
	__clear_bit(nr, addr);
	interrupt_restore(flags);
}

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static inline void __change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p ^= mask;
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long flags;

	interrupt_disable(flags);
	__change_bit(nr, addr);
	interrupt_restore(flags);
}

/**
 * __test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static inline int __test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old = *p;

	*p = old | mask;
	return (old & mask) != 0;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long flags;
	int out;

	interrupt_disable(flags);
	out = __test_and_set_bit(nr, addr);
	interrupt_restore(flags);
	return out;
}


/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static inline int __test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long flags;
	int out;

	interrupt_disable(flags);
	out = __test_and_clear_bit(nr, addr);
	interrupt_restore(flags);
	return out;
}

/* WARNING: non atomic and it can be reordered! */
static inline int __test_and_change_bit(int nr,
					    volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old = *p;

	*p = old ^ mask;
	return (old & mask) != 0;
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long flags;
	int out;

	interrupt_disable(flags);
	out = __test_and_change_bit(nr, addr);
	interrupt_restore(flags);
	return out;
}

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static inline int __test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	unsigned long flags;
	int out;

	interrupt_disable(flags);
	out = __test_bit(nr, addr);
	interrupt_restore(flags);
	return out;
}

#endif /* _ASM_GENERIC_BITOPS_NON_ATOMIC_H_ */
