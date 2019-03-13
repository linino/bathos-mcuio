#ifndef __BATHOS_ENDIAN_H__
#define __BATHOS_ENDIAN_H__

#include <machine/endian.h>

#if BYTE_ORDER==BIG_ENDIAN

static inline uint16_t to_le16(uint16_t x)
{
	return __builtin_bswap16(x);
}

static inline uint32_t to_le32(uint32_t x)
{
	return __builtin_bswap32(x);
}

static inline uint16_t to_be16(uint16_t x)
{
	return x;
}

static inline uint16_t to_be32(uint32_t x)
{
	return x;
}

#elif BYTE_ORDER==LITTLE_ENDIAN /* LITTLE_ENDIAN */

static inline uint16_t to_le16(uint16_t x)
{
	return x;
}

static inline uint32_t to_le32(uint32_t x)
{
	return x;
}

static inline uint16_t to_be16(uint16_t x)
{
	return __builtin_bswap16(x);
}

static inline uint16_t to_be32(uint32_t x)
{
	return __builtin_bswap32(x);
}

#else
#error "BYTE_ORDER MUST BE EITHER LITTLE_ENDIAN OR BIG_ENDIAN"
#endif

#define from_le16 to_le16
#define from_le32 to_le32


#endif /* __BATHOS_ENDIAN_H__ */
