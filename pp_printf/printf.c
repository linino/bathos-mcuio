/*
 * Basic printf based on vprintf based on vsprintf
 *
 * Alessandro Rubini for CERN, 2011 -- public domain
 * (please note that the vsprintf is not public domain but GPL)
 */

#include <stdarg.h>
#include <arch/bathos-arch.h>
#include <pp-printf.h>

static char print_buf[CONFIG_PRINT_BUFSIZE];

int pipe_pp_vprintf(struct bathos_pipe *p, const char * PROGMEM fmt,
		    va_list args)
{
	int ret;

	ret = pp_vsprintf(print_buf, fmt, args);
	pipe_puts(p, print_buf);
	return ret;
}

int pp_printf(const char * PROGMEM fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
#ifdef CONFIG_STDOUT_CONSOLE
	{
		const char *ptr;

		ret = pp_vsprintf(print_buf, fmt, args);
		for (ptr = print_buf; *ptr; ptr++)
			/* This will actually call console_putc() */
			putc(*ptr);
	}
#else
	ret = pipe_pp_vprintf(bathos_stdout, fmt, args);
#endif
	va_end(args);
	return ret;
}

int pp_sprintf(char *s, const char * PROGMEM fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = pp_vsprintf(s, fmt, args);
	va_end(args);
	return ret;
}


int pipe_pp_printf(struct bathos_pipe *p, const char * PROGMEM fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = pipe_pp_vprintf(p, fmt, args);
	va_end(args);

	return ret;
}
