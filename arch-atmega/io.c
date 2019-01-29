#include <bathos/bathos.h>
#include <bathos/io.h>
#include <bathos/init.h>
#include <bathos/pipe.h>
#include <bathos/shell.h>
#include <generated/autoconf.h>
#include <arch/hw.h>

#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))

int bathos_setup(void)
{
	CPU_PRESCALE(0);

	console_early_init();

	/* Turn red led on */
	timer_init();

	do_initcalls();

	/* Interrupts are enabled by the calling assembly code */
	return 0;
}

/* md command */

#define MD_LINE_LEN 8

static int md_handler(struct bathos_pipe *p, int argc, char *argv[])
{
	int addr;
	int rem;
	for (addr = 0; addr <= 0xff; addr++) {
		rem = addr % MD_LINE_LEN;
		if (rem == 0)
			pipe_printf(p, "%02x: ", addr);
		pipe_printf(p, "%02x ", *((uint8_t*)addr));
		if (rem == MD_LINE_LEN - 1)
			pipe_printf(p, "\n");
	}
	pipe_printf(p, "\n");
	return 0;
}

static void md_help(struct bathos_pipe *p, int argc, char *argv[])
{
	pipe_printf(p, "show registers status\n");
}

declare_shell_cmd(md, md_handler, md_help);
