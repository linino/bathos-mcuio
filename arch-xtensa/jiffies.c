
#include <bathos/jiffies.h>
#include <bathos/event.h>
#include <bathos/sys_timer.h>
#include <bathos/init.h>
#include <ets_sys.h>
#include <os_type.h>
#include <osapi.h>

volatile unsigned long jiffies;


static volatile os_timer_t jiffies_timer;


static void do_jiffies(void *arg)
{
	jiffies++;
	trigger_event(&event_name(hw_timer_tick), NULL);
}


static int jiffies_init(void)
{
	os_timer_setfn(&jiffies_timer, (os_timer_func_t *)do_jiffies, NULL);
	os_timer_arm(&jiffies_timer, 1000/HZ, 1);
	return 0;
}
core_initcall(jiffies_init);
