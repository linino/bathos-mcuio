#ifndef __SYS_TIMER_H__
#define __SYS_TIMER_H__

extern int sys_timer_enqueue_tick(unsigned long jif, void *data,
				  const struct event *);

extern int sys_timer_get_next_tick(unsigned long *, void **data);

declare_extern_event(hw_timer_tick);
declare_extern_event(sys_timer_tick);


#endif /* __SYS_TIMER_H__ */
