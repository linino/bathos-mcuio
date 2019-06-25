/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef __BATHOS_GPIO_H__
#define __BATHOS_GPIO_H__

#include <bathos/types.h>
#include <bathos/errno.h>
#include <arch/gpio.h>

#ifndef HAVE_GPIO_PORT_8
static inline int __gpio_set_port8(int gpio, u8 value)
{
	return -ENOSYS;
}

static inline int __gpio_get_port8(int gpio)
{
	return -ENOSYS;
}
#endif

#ifndef HAVE_GPIO_PORT_16
static inline int __gpio_set_port16(int gpio, u16 value)
{
	return -ENOSYS;
}

static inline int __gpio_get_port16(int gpio)
{
	return -ENOSYS;
}
#endif

#ifndef HAVE_GPIO_PORT_32
static inline int __gpio_set_port32(int gpio, u32 value)
{
	return -ENOSYS;
}

static inline int __gpio_get_port32(int gpio)
{
	return -ENOSYS;
}
#endif

static inline int __gpio_set_portw(int gpio, const void *in, int w)
{
	switch (w) {
	case 8:
		return __gpio_set_port8(gpio, *(const uint8_t *)in);
	case 16:
		return __gpio_set_port16(gpio, *(const uint16_t *)in);
	case 32:
		return __gpio_set_port32(gpio, *(const uint32_t *)in);
	default:
		return -ENOSYS;
	}
	/* NEVER REACHED */
	return -ENOSYS;
}

static inline int __gpio_get_portw(int gpio, void *out, int w)
{
	switch (w) {
	case 8:
		*(uint8_t *)out = __gpio_get_port8(gpio);
	case 16:
		*(uint16_t *)out = __gpio_get_port16(gpio);
	case 32:
		*(uint32_t *)out = __gpio_get_port32(gpio);
	default:
		return -ENOSYS;
	}
	/* NEVER REACHED */
	return -ENOSYS;
}

#define GPIO_EVT_RISING  0x01
#define GPIO_EVT_FALLING 0x02
#define GPIO_EVT_HIGH    0x04
#define GPIO_EVT_LOW     0x08
#define GPIO_EVT_ENABLE  0x80

/* Event data for gpio event */
struct gpio_event_data {
	uint32_t *evt_status;
	int gpio_offset;
};

#ifndef CONFIG_HAVE_GPIO_EVENTS
static inline int gpio_request_events(int gpio, int flags)
{
	return -ENOSYS;
}
#else
extern int gpio_request_events(int gpio, int flags);
#endif /* CONFIG_HAVE_GPIO_EVENTS */

/*
 * gpio_to_af() must be implemented for machines having a specific alternate
 * function for gpio selection. In case a specific machine implements
 * gpio_to_af, HAVE_GPIO_TO_AF must be defined
 */
#ifndef CONFIG_HAVE_GPIO_TO_AF
static inline int gpio_to_af(int gpio)
{
	return 0;
}
#endif /* HAVE_GPIO_TO_AF */

#endif /* __BATHOS_GPIO_H__ */
