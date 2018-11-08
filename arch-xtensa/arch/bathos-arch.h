#ifndef __ARM_BATHOS_ARCH_H__
#define __ARM_BATHOS_ARCH_H__

#include <eagle_soc.h>

#define PROGMEM

/*
 * From https://github.com/esp8266/Arduino/pull/649/files and xtensa
 * instruction set manual
 */

#define interrupt_disable(a)						\
        __asm__ __volatile__("rsil %0, 15" : "=a" (a))

#define interrupt_restore(a)						\
	__asm__ __volatile__("wsr %0,ps; isync" :: "a" (a) : "memory")

#define __isr __attribute__((section(".isr")))

void ets_isr_attach(int intr, void *handler, void *arg);

#endif /* __BATHOS_ARCH_H__ */

