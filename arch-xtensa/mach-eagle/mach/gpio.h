/*
 * Copyright (c) 2019 dog hunter LLC and the Linino community
 * Author: Davide Ciminaghi <davide@linino.org> 2019
 *
 * Linino.org is a dog hunter sponsored community project
 *
 * SPDX-License-Identifier: GPL
 */
#ifndef __GPIO_MACH_EAGLE_H__
#define __GPIO_MACH_EAGLE_H__

#include <bathos/io.h>
#include <bathos/bitops.h>
#include <bathos/errno.h>
#include <mach/hw.h>

/* https://www.esp8266.com/viewtopic.php?f=13&t=273 */

#define GPIO_BASE 0x60000300

#define GPIO_OUT	(GPIO_BASE + 0x00)
#define GPIO_OUT_SET	(GPIO_BASE + 0x04)
#define GPIO_OUT_CLEAR	(GPIO_BASE + 0x08)
#define GPIO_DIR	(GPIO_BASE + 0x0c)
#define GPIO_DIR_OUT	(GPIO_BASE + 0x10)
#define GPIO_DIR_IN	(GPIO_BASE + 0x14)
#define GPIO_IN		(GPIO_BASE + 0x18)
#define GPIO_STATUS	(GPIO_BASE + 0x1c)
#define GPIO_EVT(nr)    (GPIO_BASE + 0x28 + (nr << 2))
#define GPIO_ISET	(GPIO_BASE + 0x20)
#define GPIO_ICLR	(GPIO_BASE + 0x24)

/* From sdk's gpio.h */
typedef enum {
	GPIO_PIN_INTR_DISABLE = 0,
	GPIO_PIN_INTR_POSEDGE = 1,
	GPIO_PIN_INTR_NEGEDGE = 2,
	GPIO_PIN_INTR_ANYEDGE = 3,
	GPIO_PIN_INTR_LOLEVEL = 4,
	GPIO_PIN_INTR_HILEVEL = 5
} GPIO_INT_TYPE;

/*
 * From sdk's gpio.h too, events related gpio registers are not yet documented
 */
extern void gpio_pin_intr_state_set(int gpio, GPIO_INT_TYPE intr_state);

static inline void gpio_set(int gpio, int value)
{
	if (value)
		writel(BIT(gpio), GPIO_OUT_SET);
	else
		writel(BIT(gpio), GPIO_OUT_CLEAR);
}

static inline void gpio_dir(int gpio, int output, int value)
{
	gpio_set(gpio, value);
	if (output)
		writel(BIT(gpio), GPIO_DIR_OUT);
	else
		writel(BIT(gpio), GPIO_DIR_IN);
}

static inline int gpio_get(int gpio)
{
	return readl(GPIO_IN) & BIT(gpio);
}

static inline int gpio_to_af(int gpio)
{
	switch (gpio) {
	case 0:
		return FUNC_GPIO0;
	case 1:
		return FUNC_GPIO1;
	case 2:
		return FUNC_GPIO2;
	case 3:
		return FUNC_GPIO3;
	case 4:
		return FUNC_GPIO4;
	case 5:
		return FUNC_GPIO5;
	case 9:
		return FUNC_GPIO9;
	case 10:
		return FUNC_GPIO10;
	case 12:
		return FUNC_GPIO12;
	case 13:
		return FUNC_GPIO13;
	case 14:
		return FUNC_GPIO14;
	case 15:
		return FUNC_GPIO15;
	case 6:
	case 7:
	case 8:
	case 11:
	default:
		return -EINVAL;
	}
}

static inline unsigned long _gpio_to_periph_iomux(int gpio)
{
	switch (gpio) {
	case 0:
		return PERIPHS_IO_MUX_GPIO0_U;
	case 1:
		return PERIPHS_IO_MUX_U0TXD_U;
	case 2:
		return PERIPHS_IO_MUX_GPIO2_U;
	case 3:
		return PERIPHS_IO_MUX_U0RXD_U;
	case 4:
		return PERIPHS_IO_MUX_GPIO4_U;
	case 5:
		return PERIPHS_IO_MUX_GPIO5_U;
	case 9:
		return PERIPHS_IO_MUX_SD_DATA2_U;
	case 10:
		return PERIPHS_IO_MUX_SD_DATA3_U;
	case 12:
		return PERIPHS_IO_MUX_MTDI_U;
	case 13:
		return PERIPHS_IO_MUX_MTCK_U;
	case 14:
		return PERIPHS_IO_MUX_MTMS_U;
	case 15:
		return PERIPHS_IO_MUX_MTDO_U;
	case 6:
	case 7:
	case 8:
	case 11:
	default:
		return (unsigned long)-1;
	}
}

static inline int gpio_dir_af(int gpio, int output, int value, int afnum)
{
	unsigned long m = _gpio_to_periph_iomux(gpio);

	if ((m != (unsigned long)-1))
		PIN_FUNC_SELECT(m, afnum);
	gpio_dir(gpio, output, value);
	return 0;
}


static inline int gpio_get_dir_af(int gpio, int *output, int *value, int *afnum)
{
	if (afnum)
		*afnum = (READ_PERI_REG(gpio) >> PERIPHS_IO_MUX_FUNC_S) &
			PERIPHS_IO_MUX_FUNC;
	if (output)
		*output = readl(GPIO_DIR) & BIT(gpio);
	if (value)
		*value = readl(GPIO_OUT) & BIT(gpio);
	return 0;
}

static inline void gpio_intr_ack(uint32_t ack_mask)
{
	writel(ack_mask, GPIO_ICLR);
}

#endif /* __GPIO_MACH_EAGLE_H__ */
