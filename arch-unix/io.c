/*
 * Arch-dependent initialization and I/O functions
 * Alessandro Rubini, 2012 GNU GPL2 or later
 */
#include <stdint.h>
#include <bathos/bathos.h>
#include <bathos/cmdline.h>
#include <bathos/stdio.h>
#include <bathos/init.h>
#include <bathos/pipe.h>
#include <arch/hw.h>

int stdio_init(void)
{
	bathos_stdout = pipe_open("/dev/stdout", BATHOS_MODE_OUTPUT, NULL);
	bathos_stdin = pipe_open("/dev/stdin", BATHOS_MODE_INPUT, NULL);
	return 0;
}

subsys_initcall(stdio_init);


/* We need a main function, called by libc initialization */
int main(int argc, char **argv)
{
	parse_cmdline(argc, argv);
	/* Open stdout before getting to main */
	bathos_setup();
	bathos_main();
	return 0;
}

void console_putc(int c)
{
	write(1, &c, 1);
}

int console_init(void)
{
}
