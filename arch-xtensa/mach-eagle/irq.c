/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* esp8266 machine code */

#include <stdint.h>
#include <bathos/bathos.h>
#include <bathos/event.h>
#include <bathos/init.h>
#include <bathos/bitops.h>
#include <bathos/io.h>
#include <bathos/irq-controller.h>
#include <arch/bathos-arch.h>
#include <mach/hw.h>
#include <ets_sys.h>
#include <osapi.h>
#include <user_interface.h>

#define GPIO_NR 16


#define SPIIR (0x3ff00000 + 0x20)
#  define SPI0_INT 4
#  define SPI1_INT 7
#  define SPI2_INT 9

static void __isr spi_isr(void *arg)
{
	uint32_t istatus;

	istatus = readl(SPIIR);
	if (test_bit(SPI0_INT, &istatus))
		trigger_interrupt_event(SPI0_IRQN);
	if (test_bit(SPI1_INT, &istatus))
		trigger_interrupt_event(SPI1_IRQN);
	if (test_bit(SPI2_INT, &istatus))
		trigger_interrupt_event(SPI2_IRQN);
	/* FIXME: MORE EVENTS .... */
}

#define GPIOIR (0x60000300 + 0x1c)

static void __isr gpio_isr(void *arg)
{
	uint32_t istatus;

	istatus = readl(GPIOIR);

	if (istatus)
		trigger_interrupt_event(GPIO_IRQN);
	else
		printf("WARN: spurious gpio interrupt\n");
}

static int eagle_irq_init(void)
{
	ETS_SPI_INTR_ATTACH(spi_isr, NULL);
	ETS_GPIO_INTR_ATTACH(gpio_isr, NULL);
	return 0;
}
core_initcall(eagle_irq_init);

/*
 * esp8266 "virtual" irq controller
 */

static inline int virt_to_hw_irq(int irq)
{
	switch (irq) {
	case SPI0_IRQN:
	case SPI1_IRQN:
	case SPI2_IRQN:
		return ETS_SPI_INUM;
	case GPIO_IRQN:
		return ETS_GPIO_INUM;
	default:
		return -1;
	}
	/* NEVER REACHED */
	return -1;
}

static void __isr esp8266_enable_irq(struct bathos_irq_controller *c, int irq)
{
	unmask_irq(virt_to_hw_irq(irq));
}

static void __isr esp8266_disable_irq(struct bathos_irq_controller *c, int irq)
{
	mask_irq(virt_to_hw_irq(irq));
}

static void __isr esp8266_clear_pending_irq(struct bathos_irq_controller *c,
					    int irq)
{
	/* FIXME: THIS IS IGNORED, DON'T KNOW HOW TO DO IT */
}

static void __isr esp8266_set_pending_irq(struct bathos_irq_controller *c,
					  int irq)
{
	/* FIXME: THIS IS IGNORED, DON'T KNOW HOW TO DO IT */
}

static void __isr esp8266_mask_ack_irq(struct bathos_irq_controller *c, int irq)
{
	/* FIXME: IS THIS ENOUGH ? */
	mask_irq(virt_to_hw_irq(irq));
}

static void __isr esp8266_unmask_irq(struct bathos_irq_controller *c, int irq)
{
	/* FIXME: IS THIS ENOUGH ? */
	unmask_irq(virt_to_hw_irq(irq));
}

static struct bathos_irq_controller esp8266_virq_ctrl = {
	.enable_irq = esp8266_enable_irq,
	.disable_irq = esp8266_disable_irq,
	.clear_pending = esp8266_clear_pending_irq,
	.set_pending = esp8266_set_pending_irq,
	.mask_ack = esp8266_mask_ack_irq,
	.unmask = esp8266_unmask_irq,
};

struct bathos_irq_controller * __isr bathos_irq_to_ctrl(int irq)
{
	if (irq < 0 || irq >= CONFIG_NR_INTERRUPTS)
		return NULL;
	return &esp8266_virq_ctrl;
}
