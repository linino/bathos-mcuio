#include <stdarg.h>
#include <bathos/pipe.h>
#include <bathos/stdio.h>
#include <arch/bathos-arch.h>

extern int pipe_pp_printf(struct bathos_pipe *, const char * PROGMEM fmt, ...)
        __attribute__((format(printf,2,3)));

extern int pipe_pp_vprintf(struct bathos_pipe *,
			   const char * PROGMEM fmt, va_list args);

extern int pp_printf(const char * PROGMEM fmt, ...)
        __attribute__((format(printf,1,2)));

extern int pp_sprintf(char *s, const char * PROGMEM fmt, ...)
        __attribute__((format(printf,2,3)));

#define pp_vprintf(fmt, args) \
	pipe_pp_vprintf(bathos_stdout, fmt, args)

extern int pp_vsprintf(char *buf, const char * PROGMEM, va_list)
        __attribute__ ((format (printf, 2, 0)));

#if defined ARCH_IS_HARVARD
static inline char __get_fmt_char(const char * PROGMEM ptr)
{
	char out;

	__copy_char(&out, ptr);
	return out;
}
#else
static inline char __get_fmt_char(const char * PROGMEM ptr)
{
	return *ptr;
}
#endif



