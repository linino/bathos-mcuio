/*
 * Copyright (c) 2019 dog hunter LLC and the Linino community
 * Author: Davide Ciminaghi <davide@linino.org> 2019
 *
 * Linino.org is a dog hunter sponsored community project
 *
 * SPDX-License-Identifier: GPL
 */

#include <arch/hw.h>
#include <mach/hw.h>
#include <arch/bathos-arch.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/string.h>
#include <bathos/gpio.h>
#include <bathos/bitops.h>
#include <bathos/errno.h>
#include <bathos/init.h>
#include <bathos/irq.h>

#define GPIO_NR 16

#define GPIO_EVT_ANY_EDGE (GPIO_EVT_RISING|GPIO_EVT_FALLING)

declare_event(gpio_evt);

uint32_t events;

static struct gpio_event_data edata = {
	.evt_status = &events,
	.gpio_offset = 0,
};

void bathos_int_handler_name(GPIO_IRQN)(struct event_handler_data *d)
{
	events = readl(GPIO_STATUS);
	if (events)
		trigger_event(&event_name(gpio_evt), &edata);
	gpio_intr_ack(events);
}

int gpio_request_events(int gpio, int flags)
{
	int enable = flags & GPIO_EVT_ENABLE;

	if (!enable) {
		gpio_pin_intr_state_set(gpio, GPIO_PIN_INTR_DISABLE);
		bathos_disable_irq(GPIO_IRQN);
		pr_debug("%s: disabling irq for gpio %d\n", __func__, gpio);
		return 0;
	}
	if ((flags & GPIO_EVT_ANY_EDGE) == GPIO_EVT_ANY_EDGE) {
		gpio_pin_intr_state_set(gpio, GPIO_PIN_INTR_ANYEDGE);
		pr_debug("%s: irq on both edges for gpio %d\n", __func__,
			 gpio);
		goto end;
	}
	if (flags & GPIO_EVT_RISING) {
		gpio_pin_intr_state_set(gpio, GPIO_PIN_INTR_POSEDGE);
		pr_debug("%s: irq on rising edge for gpio %d\n", __func__,
			 gpio);
		goto end;
	}
	if (flags & GPIO_EVT_FALLING) {
		gpio_pin_intr_state_set(gpio, GPIO_PIN_INTR_NEGEDGE);
		pr_debug("%s: irq on falling edge for gpio %d\n", __func__,
			 gpio);
		goto end;
	}
	if (flags & GPIO_EVT_LOW) {
		gpio_pin_intr_state_set(gpio, GPIO_PIN_INTR_LOLEVEL);
		pr_debug("%s: irq on low level for gpio %d\n", __func__,
			 gpio);
		goto end;
	}
	if (flags & GPIO_EVT_HIGH) {
		gpio_pin_intr_state_set(gpio, GPIO_PIN_INTR_HILEVEL);
		pr_debug("%s: irq on high level for gpio %d\n", __func__,
			 gpio);
		goto end;
	}
end:
	bathos_enable_irq(GPIO_IRQN);	
	return 0;
}
