/*
 * Main function: a welcome message and a simple scheduler
 * Alessandro Rubini, 2009 GNU GPL2 or later
 */
#include <bathos/bathos.h>
#include <bathos/jiffies.h>
#include <bathos/event.h>
#include <bathos/idle.h>
#include <bathos/errno.h>
#include <bathos/sys_timer.h>
#include <arch/hw.h>

int bathos_errno;

/*
 * Dummy default idle
 */
void __attribute__((weak)) idle(void)
{
	trigger_event(&event_name(hw_timer_tick), NULL);
}

void __attribute__((weak)) bathos_loop(void)
{
	while(1) {
		while(pending_events())
			handle_events();
		idle();
	}
}

#ifndef CONFIG_BOARD
#define BOARD ""
#else
#define BOARD CONFIG_BOARD
#endif

int bathos_main(void)
{
#if !CONFIG_CONSOLE_NULL
	printf("Hello, Bathos is speaking (%s built on " __DATE__ "), %s\n",
	       BATHOS_GIT, BOARD);
#endif
	bathos_loop();
	return 0;
}
