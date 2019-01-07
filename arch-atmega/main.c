/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

/*
 * Main for the avr. Things we want to do with interrupts enabled before
 * calling the regular bathos main function
 */
#include <arch/hw.h>
#include <arch/gpio.h>
#include <generated/autoconf.h>
#include <bathos/delay.h>
#include <bathos/bathos.h>
#include <avr/wdt.h>

int avr_bathos_main(void)
{
	MCUSR = 0;
	wdt_disable();
	udelay_init();
#if defined CONFIG_ATMEGA_USB_UART
	/* Without this, USB doesn't seem to work well */
	mdelay(200);
#endif
#if defined CONFIG_BOARD_MODULINO
	/* Turn off esp8266 */
	gpio_dir_af(GPIO_NR(ATMEGA_PORTE, 1), 1, 0, 0);
#endif	
	return bathos_main();
}
