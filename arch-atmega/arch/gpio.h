/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

/*
 * ATMEGA gpio definitions
 */
#ifndef __ATMEGA_GPIO_H__
#define __ATMEGA_GPIO_H__

#include <bathos/types.h>
#include <bathos/errno.h>
#include <generated/autoconf.h>
#include <avr/io.h>

#if defined CONFIG_MACH_ATMEGA32U4
#include <arch/gpio-atmega32u4.h>
#endif
#if defined CONFIG_MACH_ATMEGA8
#include <arch/gpio-atmega8.h>
#endif
#if defined CONFIG_MACH_ATMEGA328P
#include <arch/gpio-atmega328p.h>
#endif

/* Can handle 8 gpios belonging to the same port with __gpio_set_port */
#define HAVE_GPIO_PORT_8

#define GPIO_PORT_WIDTH 8
#define GPIO_NR(port, bit) ((port) * GPIO_PORT_WIDTH + (bit))
#define GPIO_PORT(nr)      ((nr) / GPIO_PORT_WIDTH)
#define GPIO_BIT(nr)       ((nr) % GPIO_PORT_WIDTH)

static inline void gpio_set(int gpio, int value)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	int addr = PORTS_BASE + port * 3 + 2;
	if (value)
		_SFR_IO8(addr) |= (1 << pin);
	else
		_SFR_IO8(addr) &= ~(1 << pin);
}

static inline void __gpio_set(int gpio, u32 value)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	int addr = PORTS_BASE + port * 3 + 2;
	if (value)
		_SFR_IO8(addr) |= (u8)value;
	else
		_SFR_IO8(addr) &= ~(1 << pin);
}

static inline int __gpio_set_port8(int gpio, u8 value)
{
	int port = GPIO_PORT(gpio);
	int addr = PORTS_BASE + port * 3 + 2;
	if (gpio % GPIO_PORT_WIDTH)
		return -EINVAL;
	_SFR_IO8(addr) = (u8)value;
	return 0;
}

static inline void gpio_dir(int gpio, int output, int value)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	int data_addr = PORTS_BASE + port * 3 + 2;
	int dir_addr = PORTS_BASE + port * 3 + 1;
	uint8_t mask = (1 << pin);
	if (value)
		_SFR_IO8(data_addr) |= mask;
	else
		_SFR_IO8(data_addr) &= ~mask;
	if (output)
		_SFR_IO8(dir_addr) |= mask;
	else {
		/*
		 * TEMPORARY HACK: ENABLE INPUT PULLUP BY SETTING DATA VALUE
		 * TO 1
		 */
		_SFR_IO8(data_addr) |= mask;
		_SFR_IO8(dir_addr) &= ~mask;
	}
}

static inline int gpio_get(int gpio)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	uint8_t mask = (1 << pin);
	int addr = PORTS_BASE + port * 3;
	return _SFR_IO8(addr) & mask ? 1 : 0;
}

static inline u32 __gpio_get(int gpio)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	int addr = PORTS_BASE + port * 3;
	return _SFR_IO8(addr) & (1 << pin);
}

static inline u8 __gpio_get_port8(int gpio)
{
	int port = GPIO_PORT(gpio);
	int addr = PORTS_BASE + port * 3;
	if (gpio % GPIO_PORT_WIDTH)
		return -EINVAL;
	return _SFR_IO8(addr);
}

static inline int gpio_dir_af(int gpio, int output, int value, int afnum)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	int addr = PORTS_BASE + port * 3 + 1;
	if (afnum != 0)
		/* Unsupported at the moment */
		return -EINVAL;
	gpio_set(gpio, value);
	if (output)
		_SFR_IO8(addr) |= (1 << pin);
	else
		_SFR_IO8(addr) &= ~(1 << pin);
	return 0;
}

static inline int gpio_get_dir_af(int gpio, int *output, int *value, int *afnum)
{
	int port = GPIO_PORT(gpio);
	int pin = GPIO_BIT(gpio);
	int addr = PORTS_BASE + port * 3 + 1;
	if (afnum)
		/* Unsupported at the moment */
		*afnum = 0;
	if (value)
		*value = gpio_get(gpio);
	if (output)
		*output = _SFR_IO8(addr) & (1 << pin) ? 1 : 0;
	return 0;
}


#endif /* __ATMEGA_GPIO_H__ */
