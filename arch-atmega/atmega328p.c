
#include <bathos/bathos.h>
#include <bathos/jiffies.h>
#include <bathos/event.h>
#include <bathos/idle.h>
#include <bathos/sys_timer.h>

#include <arch/hw.h>
#include <avr/interrupt.h>
#include <avr/io.h>

#define PRESCALER 256
#define TCCR0B_PRESCALER_256 (1 << CS02)

void timer_init(void)
{
	/* prescaler: 256, 16MHz / 256 = 62500Hz */
	TCCR0B = 1 << CS02;
	/* 62500Hz / 250 = 250HZ */
	OCR0A = 250;
	TIMSK0 = 1 << TOIE0;
}

volatile unsigned long jiffies;

ISR(TIMER0_OVF_vect, __attribute__((section(".text.ISR"))))
{
	jiffies++;
	trigger_event(&event_name(hw_timer_tick), NULL);
}
