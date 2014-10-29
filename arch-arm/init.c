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
#include <generated/autoconf.h>
#include <arch/hw.h>
#include <stdint.h>

volatile unsigned long __attribute__((weak)) jiffies;

/* Dummy start function (replaced by machine's romboot_start in boot.S */
void __attribute__((weak)) _romboot_start(void)
{
}

int bathos_setup(void)
{
	console_early_init();
	do_initcalls();
	return 0;
}
