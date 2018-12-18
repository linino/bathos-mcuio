#include <stdint.h>

#include <avr/pgmspace.h>

#ifndef THOS_QUARTZ
#define THOS_QUARTZ		(16UL * 1000 * 1000)
#endif /* THOS_QUARTZ */
#ifndef HZ
#if (MCU_atmega32u4==1)
#define HZ			(THOS_QUARTZ / 256 / 250) /* 250 */
#else
#define HZ			(THOS_QUARTZ / 256 / 256) /* 244 (+.140625) */
#endif
#endif /* HZ */

/* Make avr-libc happy */
#define F_CPU THOS_QUARTZ

#include <avr/io.h>

extern void timer_init(void);
extern void pll_init(void);

#ifdef MCU_atmega8
#include <arch/hw_atmega8.h>
#elif MCU_atmega32u4
#include <arch/hw_atmega32u4.h>
#elif MCU_atmega328p
#include <arch/hw_atmega328p.h>
#else
#error "Invalid atmel MCU defined"
#endif
