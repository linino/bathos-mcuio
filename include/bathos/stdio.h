/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef __BATHOS_STDIO_H__
#define __BATHOS_STDIO_H__
#include <arch/bathos-arch.h>
#include <stdarg.h>

#ifndef NULL
#define NULL 0
#endif

#ifndef PSTR
#define PSTR(a) a
#endif

#include <generated/autoconf.h>

struct bathos_pipe;

/* These 4 are actually pp_printf and friends */
extern int __printf(const char * PROGMEM fmt, ...)
        __attribute__((format(printf,1,2)));

#define printf(a, args...) __printf(PSTR(a), ##args)

extern int __sprintf(char *s, const char * PROGMEM fmt, ...)
        __attribute__((format(printf,2,3)));

#define sprintf(a, args...) __sprintf(PSTR(a), ##args)

extern int __vprintf(const char * PROGMEM fmt, va_list args);

#define vprintf(a, args...) __vprintf(PSTR(a), ##args)

extern int __vsprintf(char *buf, const char * PROGMEM, va_list)
        __attribute__ ((format (printf, 2, 0)));

extern int __pipe_printf(struct bathos_pipe *, const char * PROGMEM fmt, ...)
	__attribute__((format(printf,2,3)));

#define pipe_printf(a, b, args...) __pipe_printf(a, b, ##args)

#define vsprintf(a, args...) __vsprintf(PSTR(a), ##args)

#ifndef DEBUG
#define pr_debug(a, args...)
#else
#define pr_debug(a, args...) printf(a, ##args)
#endif

/* Puts is not actually "standard", as it doesn't add the trailing newline */
extern void putc(int c);

struct bathos_pipe;

extern struct bathos_pipe *bathos_stdout;
extern struct bathos_pipe *bathos_stdin;

extern int pipe_puts(struct bathos_pipe *, const char *s);

static inline int puts(const char *s)
{
	return pipe_puts(bathos_stdout, s);
}

extern void console_putc(int c);
extern int console_init(void);

#if defined CONFIG_EARLY_CONSOLE
extern int console_early_init(void);
#else
static inline int console_early_init(void)
{
	return 0;
}
#endif

#endif /* __BATHOS_STDIO_H__ */
