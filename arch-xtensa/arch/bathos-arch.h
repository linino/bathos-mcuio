#ifndef __ARM_BATHOS_ARCH_H__
#define __ARM_BATHOS_ARCH_H__

#include <stdint.h>
#include <eagle_soc.h>

#define PROGMEM

void ets_isr_mask(uint32_t v);
void ets_isr_unmask(uint32_t v);

/*
 * From https://github.com/esp8266/Arduino/pull/649/files and xtensa
 * instruction set manual
 */

#define interrupt_disable(a)						\
        __asm__ __volatile__("rsil %0, 15" : "=a" (a))

#define interrupt_restore(a)						\
	__asm__ __volatile__("wsr %0,ps; isync" :: "a" (a) : "memory")

static inline unsigned get_intenable(void)
{
	uint32_t intenable;

	__asm__ __volatile__("rsr.intenable %0" : "=r"(intenable));
	return intenable;
}

static inline void set_intenable(unsigned v)
{
	__asm__ __volatile__("wsr.intenable %0" :: "r"(v));
}

static inline void mask_irq(int n)
{
	ets_isr_mask(1 << n);
}

static inline void unmask_irq(int n)
{
	ets_isr_unmask(1 << n);
}

#define __isr __attribute__((section(".isr")))

#endif /* __BATHOS_ARCH_H__ */

