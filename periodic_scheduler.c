
#include <bathos/bathos.h>
#include <bathos/jiffies.h>
#include <bathos/event.h>
#include <bathos/init.h>
#include <bathos/sys_timer.h>
#include <arch/hw.h>

declare_event(sys_timer_tick);

static int pts_init(void)
{
	unsigned long now = jiffies;
	struct bathos_task *p;
	for (p = __task_begin; p < __task_end; p++) {
		if (p->init) p->init(p->arg);
	}
	for (p = __task_begin; p < __task_end; p++) {
		p->release += now + HZ;
		if (sys_timer_enqueue_tick(p->release - now, p, NULL) < 0)
			return -1;
	}
	return 0;
}

core_initcall(pts_init);

static void pts_handle(struct event_handler_data *d)
{
	struct bathos_task *p = d->data;
	unsigned long now;
	p->arg = p->job(p->arg);
	p->release += p->period;
	now = jiffies;
	if (time_before_eq(p->release, now))
		p->release = now + 1;
	sys_timer_enqueue_tick(p->release - now, p, NULL);
}

declare_event_handler(sys_timer_tick, NULL, pts_handle, NULL);
