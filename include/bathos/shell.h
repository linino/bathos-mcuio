#ifndef __SHELL_H__
#define __SHELL_H__

#include <bathos/event.h>

/* simple bathos shell */

struct bathos_pipe;

struct shell_cmd {
	const char *str;
	int (*handler)(struct bathos_pipe *, int argc, char *argv[]);
	void (*help)(struct bathos_pipe *, int argc, char *argv[]);
};

#ifndef cat
#define cat(a,b) a##b
#endif
#ifndef xcat
#define xcat(a,b) cat(a,b)
#endif
#ifndef str
#define str(a) #a
#endif
#ifndef xstr
#define xstr(a) str(a)
#endif

#define cmd_name(n) xcat(shell_cmd_, n)

#define declare_shell_cmd(n, hl, hp) \
	const struct shell_cmd __attribute__((section(".shell_cmds"))) \
	cmd_name(n) = {							\
		.str = xstr(n),						\
		.handler = hl,					\
		.help = hp,						\
	}

declare_extern_event(shell_input_ready);
declare_extern_event(shell_termination);
declare_extern_event(shell_start);


#endif /* __SHELL_H__ */
