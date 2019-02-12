
#include <avr/interrupt.h>
#include <bathos/bathos.h>
#include <bathos/event.h>

#ifndef VECNO
#error "Please compile this file with VECNO defined"
#endif

/*
 * Default ISR handler, just returns
 */
void __attribute__((naked, noreturn, weak, section(".text.ISR")))
xcat(__vector_,VECNO)(void)
{
	__asm__ __volatile__ ("reti"::);
}
