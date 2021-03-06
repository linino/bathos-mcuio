/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#include <bathos/bathos.h>
#include <bathos/io.h>
#include <bathos/init.h>
#include <bathos/pipe.h>
#include <bathos/shell.h>
#include <bathos/delay.h>
#include <generated/autoconf.h>
#include <arch/hw.h>
#include <stdint.h>

volatile unsigned long __attribute__((weak)) jiffies;
volatile unsigned long __attribute__((weak)) _sdata;
volatile unsigned long __attribute__((weak)) _erom;

int bathos_setup(void)
{
	console_early_init();
	do_initcalls();
	return 0;
}

/*
 * We get here after bathos setup, but with interrupts enabled
 * This is the place were initializations needing interrupts enabled
 * can be done
 */
int arm_bathos_main(void)
{
	if (udelay_init() < 0)
		printf("Warning: error in udelay init\n");
	return bathos_main();
}
