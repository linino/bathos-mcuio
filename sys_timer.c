/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

//#include <stdlib.h>
#include <bathos/errno.h>
#include <bathos/interrupt.h>

#include <bathos/bathos.h>
#include <bathos/event.h>
#include <bathos/init.h>
#include <bathos/types.h>
#include <bathos/jiffies.h>
#include <bathos/sys_timer.h>
#include <bathos/allocator.h>
#include <linux/list.h>

declare_event(hw_timer_tick);

static struct list_head scheduled_ticks;

struct scheduled_tick {
	unsigned long when;
	void *data;
	const struct event *evt;
	struct list_head list;
};

static int system_timer_init(void)
{
#if !CONFIG_CONSOLE_NULL
	printf("%s\n", __func__);
#endif
	INIT_LIST_HEAD(&scheduled_ticks);
	return 0;
}

core_initcall(system_timer_init);

static struct scheduled_tick *alloc_tick(void)
{
	struct scheduled_tick *out;

	out = bathos_alloc_buffer(sizeof(*out));
	return out;
}

static void free_tick(struct scheduled_tick *t)
{
	list_del(&t->list);
	bathos_free_buffer(t, sizeof(*t));
}

int sys_timer_enqueue_tick(unsigned long evt_jiffies, void *data,
			   const struct event *_evt)
{
	struct scheduled_tick *t, *ptr;
	const struct event *evt = _evt ? _evt : &event_name(sys_timer_tick);
	int flags;
	t = alloc_tick();
	if (!t)
		return -ENOMEM;
	t->when = jiffies + evt_jiffies;
	t->data = data;
	t->evt = evt;
	interrupt_disable(flags);
	if (list_empty(&scheduled_ticks)) {
		list_add(&t->list, &scheduled_ticks);
		interrupt_restore(flags);
		return 0;
	}
	list_for_each_entry(ptr, &scheduled_ticks, list) {
		if (time_before(t->when, ptr->when)) {
			ptr->when -= t->when;
			list_add_tail(&t->list, &ptr->list);
			interrupt_restore(flags);
			return 0;
		}
		t->when -= ptr->when;
	}
	list_add_tail(&t->list, &ptr->list);
	interrupt_restore(flags);
	return 0;
}

void on_hw_timer_tick(struct event_handler_data *d)
{
	struct scheduled_tick *next, *tmp;
	list_for_each_entry_safe(next, tmp, &scheduled_ticks, list) {
		if (time_after(next->when, jiffies))
			break;
		trigger_event(next->evt, next->data);
		if (!list_is_last(&next->list, &scheduled_ticks)) {
			struct scheduled_tick *t;
			t = list_entry(next->list.next,
				       struct scheduled_tick, list);
			t->when += jiffies;
		}
		free_tick(next);
	}
}

declare_event_handler(hw_timer_tick, NULL, on_hw_timer_tick, NULL);
