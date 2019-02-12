/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

/*
 * From mm/page_alloc.c, Linux Kernel
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 * the following equation:
 *     B2 = B1 ^ (1 << O)
 * For example, if the starting buddy (buddy2) is #8 its order
 * 1 buddy is #10:
 *     B2 = 8 ^ (1 << 1) = 8 ^ 2 = 10
 *
 * 2) Any buddy B will have an order O+1 parent P which
 * satisfies the following equation:
 *     P = B & ~(1 << O)
 *
 * Level 1, buddy 0 -> B2 = 0 ^ (1 << 1) = 2, parent = 0 & ~(1 << 1) = 0
 *          buddy 2 -> B2 = 2 ^ (1 << 1) = 0, parent = 2 & ~(1 << 1) = 0
 *          buddy 4 -> B2 = 4 ^ (1 << 1) = 6, parent = 4 & ~(1 << 1) = 4
 *          buddy 6 -> B2 = 6 ^ (1 << 1) = 4, parent = 6 & ~(1 << 1) = 4
 * Level 0, buddy 1, parent = 1 & ~(1 << 0) = 0
 *          buddy 2, parent = 2 & ~(1 << 0) = 2
 *
 * +---+
 * | 0 | 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16
 * +---+
 * | 1 | 0   2   4   6   8   10    12    14    16
 * +---+
 * | 2 | 0       4       8         12          16
 * +---+
 * | 3 | 0               8                     16
 * +---+
 * | 4 | 0                                     16
 * +---+
 *
 * A bitmask is used to record the state of each buffer at each level:
 *
 * * Bit is 0 -> buffer is not available (either because it belongs to another
 *               order or because it has been allocated
 * * Bit is 1 -> buffer is available
 *
 * We need (2 ^ num_orders) - 1 bits (rounded up to 2 ^ num_orders)
 *
 */

#include <stdint.h>
#include <bathos/stdio.h>
#include <bathos/bitops.h>
#include <bathos/allocator.h>
#include <bathos/ffs.h>
#include <bathos/ffz.h>
#include <arch/bathos-arch.h>

/*
 * These depend on BITS_PER_LONG and BATHOS_NORDERS, they're automatically
 * generated by allocator_aux_gen
 */
extern const int PROGMEM __bitmap_len[BATHOS_NORDERS];
extern const unsigned long * const PROGMEM __bitmap_mask[BATHOS_NORDERS];
extern const int PROGMEM __bitmap_mask_ffs[BATHOS_NORDERS];

/* Bitmap representing buffers */
#define BATHOS_MAP_NBITS ((1 << (BATHOS_NORDERS)))
unsigned long __attribute__ ((aligned (4))) __bitmap[BATHOS_MAP_NBITS/(BITS_PER_LONG)];

/* Memory managed by this allocator */
uint8_t __attribute__ ((aligned (4))) mem[BATHOS_ALLOCATOR_MEMORY];

static char __initialized;

#ifdef DEBUG
static inline void __print_bitmap(void)
{
	int i;

	pr_debug("__bitmap = ");
	for (i = 0; i < (BATHOS_MAP_NBITS >> BITS_PER_LONG_LOG2); i++)
		pr_debug("0x%08x ", __bitmap[i]);
	pr_debug("\n");
}
#else
static inline void __print_bitmap(void)
{
}
#endif

static inline int __size_to_order(int size)
{
	int i;

	for (i = 0; i <= BATHOS_MAX_ORDER; i++)
		if (size <= BATHOS_BUF_SIZE(i))
			return i;
	return -1;
}

/* See comments on top of this file */
static inline int __get_buddy(int index, int order)
{
	return index ^ (1 << order);
}

/* See comments on top of this file */
static inline int __get_parent(int index, int order)
{
	return index & ~(1 << order);
}

static inline int __is_free(int index, int order)
{
	int nr = __index_to_bit(index, order);

	return test_bit(nr, __bitmap);
}

static inline void __mark_unavailable(int index, int order)
{
	int nr = __index_to_bit(index, order);

	pr_debug("marking %d@%d as unavailable\n", index, order);
	pr_debug("clearing bit %d\n", nr);
	clear_bit(nr, __bitmap);
	__print_bitmap();
}

static inline void __mark_available(int index, int order)
{
	int nr = __index_to_bit(index, order);

	pr_debug("marking %d@%d as available\n", index, order);
	pr_debug("setting bit %d\n", nr);
	set_bit(nr, __bitmap);
	__print_bitmap();
}

/* Returns length of an order's bitmap in u32 units */
/*
  0     5        31 0    6         63
  +-----+----------+-----+-----------+

  nbits = 31 - 5 + 1 + 6 - 0 + 1 = 27 + 7 = 34

  start_bit % 32 = 5 -> out = 1, nbits = 32 - 5 = 27
  out += 0 -> 1
  nbits % 32 = 27 -> out = 2
*/

#if !defined ARCH_IS_HARVARD
static inline int __order_bitmap_len(int order)
{
	return __bitmap_len[order];
}

static inline const unsigned long * PROGMEM __order_to_mask(int order,
							    unsigned long *ptr)
{
	return __bitmap_mask[order];
}

static inline int __order_to_bitmap_mask_ffs(int order)
{
	return __bitmap_mask_ffs[order];
}
#else
static inline int __order_bitmap_len(int order)
{
	int out;

	__copy_int(&out, &__bitmap_len[order]);
	pr_debug("__order_bitmap_len(%d) = %d\n", order, out);
	return out;
}

static const unsigned long * __order_to_mask(int order,
					     unsigned long *ptr)
{
	int i, l = __order_bitmap_len(order);
	const unsigned long * PROGMEM ulp;

	memcpy_p(&ulp, &__bitmap_mask[order], sizeof(ulp));

	for (i = 0; i < l; i++)
		__copy_ulong(ptr + i, ulp + i);

	pr_debug("__order_to_mask(%d, %p) = %lx\n", order, ptr, *ptr);
	return (const unsigned long *)ptr;
}

static inline int __order_to_bitmap_mask_ffs(int order)
{
	int out;

	__copy_int(&out, &__bitmap_mask_ffs[order]);
	return out;
}
#endif

static inline unsigned long *__order_bitmap(int order)
{
	pr_debug("%s: __index_to_bit(0, %d) = %d\n",
		 __func__, order, __index_to_bit(0, order));
	return &__bitmap[__index_to_bit(0, order) >> BITS_PER_LONG_LOG2];
}

/*
 * int __bit_to_index(int bit, int order)
 *
 * @bit: index of bit @order order (not absolute bit index)
 * @order: guess what
 */
static inline int __bit_to_index(int bit, int order)
{
	int out;

	pr_debug("__bit_to_index(%d, %d)\n", bit, order);
	out = bit << order;
	pr_debug("__bit_to_index: out = %d\n", out);
	return out;
}

static inline void *__find_free_buf(int order, int *out_index)
{
	int l = __order_bitmap_len(order), i, bit, index = 0;
	static unsigned long __mask[BATHOS_NBUFS(0) >> BITS_PER_LONG_LOG2];
	const unsigned long * mask = __order_to_mask(order, __mask);
	unsigned long *v = __order_bitmap(order);
	void *out;

	pr_debug("__find_free_buf(%d), l = %d\n", order, l);
	for (i = 0; i < l; i++) {
		pr_debug("__find_free_buf: v = %p, v[%d] = %lx & mask[%d] = %lx\n",
			 v, i, v[i], i, mask[i]);
		pr_debug("__find_free_buf: v[i] & mask[i] = %lx\n",
			 v[i] & mask[i]);
		if (v[i] & mask[i]) {
			int mask_ffs = __order_to_bitmap_mask_ffs(order);

			bit = (ffs(v[i] & mask[i]) - 1) - mask_ffs +
			    (i << BITS_PER_LONG_LOG2);
			pr_debug("__find_free_buf: bit %d is set\n", bit);
			pr_debug("ffs(mask[%d]) = %d\n", order, mask_ffs);
			index = __bit_to_index(bit, order);
			pr_debug("index = %d\n", index);
			break;
		}
	}
	if (i >= l)
		return NULL;

	*out_index = index;
	out = mem + *out_index * BATHOS_BUF_SIZE(0);
	pr_debug("__find_free_buf: found buffer %d, %p\n", *out_index, out);
	return out;
}

/*
 * Try coalescing buffer index and its buddy at order order
 */
static void __coalesce_buffers(int index, int order)
{
	int buddy, parent;

	pr_debug("__coalesce_buffers(%d, %d)\n", index, order);
	if (order == BATHOS_MAX_ORDER) {
		pr_debug("__coalesce_buffers, order == MAX_ORDER\n");
		return;
	}

	buddy = __get_buddy(index, order);
	pr_debug("__coalesce_buffers: buddy = %d\n", buddy);

	if (!__is_free(buddy, order)) {
		pr_debug("__coalesce_buffers: buddy is not available\n");
		/* Buddy is not free, nothing to do */
		return;
	}

	parent = __get_parent(index, order);
	pr_debug("__coalesce_buffers: parent = %d\n", parent);

	/* Remove buffers from this order, mark order + 1 parent as available */
	__mark_unavailable(index, order);
	__mark_unavailable(buddy, order);
	__mark_available(parent, order + 1);
	__coalesce_buffers(parent, order + 1);
}

static void __split_buffers(int min_order, int order)
{
	int index;

	pr_debug("__split_buffers(%d, %d)\n", min_order, order);
	if (!__find_free_buf(order, &index)) {
		/* No buffers at this order, nothing to do */
		pr_debug("__split_buffers: no buffer at this order\n");
		return;
	}
	/* Split buffer into two (order - 1) buddies */
	__mark_unavailable(index, order);
	__mark_available(index, order - 1);
	__mark_available(__get_buddy(index, order - 1), order - 1);
	if (order - min_order > 1) {
		pr_debug("__split_buffers: recursion\n");
		__split_buffers(min_order, order - 1);
	}
	pr_debug("__split_buffers(%d, %d) returns\n", min_order, order);
}

void *bathos_alloc_buffer(int size)
{
	int i, order = __size_to_order(size), index;
	void *out = NULL;

	if (!__initialized) {
		bathos_alloc_init();
		__initialized = 1;
	}

	pr_debug("bathos_alloc_buffer(%d), order = %d\n", size, order);
	if (order < 0)
		/* Illegal size */
		return NULL;

	for (i = order; !(out = __find_free_buf(order, &index)) &&
		     i < BATHOS_MAX_ORDER;
	     i++, __split_buffers(order, i));
	pr_debug("bathos_alloc_buffer: out = %p\n", out);
	if (!out)
		return NULL;

	__mark_unavailable(index, order);
	return out;
}

void bathos_free_buffer(void *b, int size)
{
	int offset = (uint8_t *)b - mem, order = __size_to_order(size);
	unsigned int index;

	pr_debug("bathos_free_buffer(%p)\n", b);

	if (offset < 0 || offset >= sizeof(mem)) {
		printf("__bathos_free_buffer: invalid ptr\n");
		return;
	}
	if (offset & (1 << (order - 1))) {
		printf("__bathos_free_buffer: unaligned ptr\n");
		return;
	}
	index = offset / BATHOS_BUF_SIZE(0);
	pr_debug("__bathos_free_buffer: index = %d, order = %d\n",
		 index, order);
	__mark_available(index, order);
	__coalesce_buffers(index, order);
}

int bathos_alloc_init(void)
{
	__mark_available(0, BATHOS_MAX_ORDER);
	return 0;
}
