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
#ifdef CONFIG_TASK_LININOIO
#include <lininoio.h>
#include <bathos/lininoio-internal.h>
#include <bathos/lininoio-dev.h>
#endif /* CONFIG_TASK_LININOIO */
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

#ifdef CONFIG_TASK_LININOIO
/*
 * LininoIO raw channel
 */
static const struct lininoio_channel_descr __attribute__((used)) cdescr = {
	/* Console core 0 (test !) */
	.contents_id = LININOIO_PROTO_CONSOLE,
};

static const struct lininoio_channel_ops __attribute__((used)) cops = {
	.input = lininoio_dev_input,
};

static struct lininoio_channel_data __attribute__((used)) cdata;

declare_lininoio_channel(lininoio0, &cdescr, &cops, &cdata);

#endif /* CONFIG_TASK_LININOIO */
