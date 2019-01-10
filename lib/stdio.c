#include <bathos/stdio.h>
#include <bathos/init.h>
#include <bathos/pipe.h>

struct bathos_pipe *bathos_stdout;
struct bathos_pipe *bathos_stdin;

/*
 * Dummy console (used when no console is selected and console_putc()
 * is not available
 */
#ifdef CONFIG_CONSOLE_NULL
void console_putc(int c)
{
}

#ifdef CONFIG_EARLY_CONSOLE
int console_early_init(void)
{
	return 0;
}
#endif /* CONFIG_EARLY_CONSOLE */

#else /* !CONFIG_CONSOLE_NULL */

#ifdef CONFIG_EARLY_CONSOLE
int console_early_init(void)
{
	return console_init();
}
#else
rom_initcall(console_init);
#endif

#endif

void pipe_putc(struct bathos_pipe *p, int _c)
{
	char c = _c;

	if (p)
		pipe_write(p, (char *)&c, 1);
}

#ifdef CONFIG_STDOUT_CONSOLE
void __attribute__((weak)) putc(int c)
{
	if (c == '\n')
		console_putc('\r');
	console_putc(c);
}
#else
void __attribute__((weak)) putc(int c)
{
	if (c == '\n')
		putc('\r');
#ifdef CONFIG_COPY_STDOUT_TO_CONSOLE
	console_putc(c);
#endif
	pipe_putc(bathos_stdout, c);
}
#endif

#ifndef CONFIG_STDOUT_CONSOLE
static inline void __stdout_open(void)
{
	bathos_stdout = pipe_open(CONFIG_STDOUT, BATHOS_MODE_OUTPUT, NULL);
}
#else
static inline void __stdout_open(void)
{
}
#endif

#ifndef CONFIG_STDIN_CONSOLE
static inline void __stdin_open(void)
{
	bathos_stdin = pipe_open(CONFIG_STDIN, BATHOS_MODE_INPUT, NULL);
}
#else
static inline void __stdin_open(void)
{
}
#endif

static int stdio_init(void)
{
	__stdout_open();
	__stdin_open();
	return 0;
}
core_initcall(stdio_init);

int pipe_puts(struct bathos_pipe *p, const char *s)
{
	while (*s)
		pipe_putc (p, *s++);
	return 0;
}
