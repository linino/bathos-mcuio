#include <bathos/bathos.h>
#include <bathos/jiffies.h>
#include <bathos/event.h>
#include <bathos/idle.h>
#include <bathos/sys_timer.h>
#include <bathos/pipe.h>
#include <arch/hw.h>
#include <arch/pipe.h>

#include <bathos/errno.h>
#include <sys/select.h>

static fd_set rd_fdset;
static fd_set wr_fdset;
static int nfds;

static void trigger_event_for_pipe(struct bathos_pipe *p)
{
	if (p->mode & BATHOS_MODE_INPUT)
		pipe_dev_trigger_event(p->dev, &evt_pipe_input_ready);
	if (p->mode & BATHOS_MODE_OUTPUT)
		pipe_dev_trigger_event(p->dev, &evt_pipe_output_ready);
}

/*
 * idle implementation for unix, select on stdin with a timeout for detecting
 * events
 */
void idle(void)
{
	int stat;
	struct timeval next_timeout;
	fd_set __rdfds = rd_fdset;

	next_timeout.tv_usec = ((1000000) / HZ*2) % 1000000;
	next_timeout.tv_sec = ((1000000) / HZ*2) / 1000000;
	stat = select(nfds, &__rdfds, NULL, NULL, &next_timeout);
	if (stat < 0 && bathos_errno == EINTR)
		return ;
	if (stat < 0) {
		sleep(1);
		return;
	}
	if (stat > 0) {
		int i;
		for (i = 0; i < nfds; i++) {
			struct bathos_pipe *p;
			if (!FD_ISSET(i, &__rdfds))
				continue;
			p = unix_fd_to_pipe(i);
			trigger_event_for_pipe(p);
		}
		return;
	}
	trigger_event(&event_name(hw_timer_tick), NULL);
}

static int pipe_opened_init(struct event_handler_data *data)
{
	FD_ZERO(&rd_fdset);
	FD_ZERO(&wr_fdset);
	return 0;
}

static void pipe_opened_handle(struct event_handler_data *data)
{
	struct bathos_pipe *p = data->data;
	struct bathos_dev *dev;
	struct arch_unix_pipe_data *adata;
	if (!p)
		return;
	dev = p->dev;
	if (!dev)
		return;
	adata = dev->arch_priv;
	if (!adata)
		return;
	if (p->mode & BATHOS_MODE_OUTPUT)
		FD_SET(adata->fd, &wr_fdset);
	if (p->mode & BATHOS_MODE_INPUT)
		FD_SET(adata->fd, &rd_fdset);
	nfds = max(nfds, adata->fd + 1);
}

static int __recalc_nfds(void)
{
	int i, out = -1;
	for (i = 0; i < 255; i++)
		if (FD_ISSET(i, &rd_fdset) || FD_ISSET(i, &wr_fdset))
			out = i+1;
	return out;
}

static void pipe_closed_handle(struct event_handler_data *data)
{
	struct bathos_pipe *p = data->data;
	struct bathos_dev *dev;
	struct arch_unix_pipe_data *adata;
	printf("%s %d, pipe = %p\n", __func__, __LINE__, p);
	if (!p)
		return;
	dev = p->dev;
	if (!dev)
		return;
	adata = dev->arch_priv;
	if (!adata)
		return;
	if (p->mode & BATHOS_MODE_OUTPUT)
		FD_CLR(adata->fd, &wr_fdset);
	if (p->mode & BATHOS_MODE_INPUT)
		FD_CLR(adata->fd, &rd_fdset);
	if ((adata->fd + 1) == nfds)
		nfds = __recalc_nfds();
	printf("%s %d, nfds = %d\n", __func__, __LINE__, nfds);
}


declare_event_handler(input_pipe_opened, pipe_opened_init,
		      pipe_opened_handle, NULL);
declare_event_handler(output_pipe_opened, pipe_opened_init,
		      pipe_opened_handle, NULL);
declare_event_handler(input_pipe_closed, NULL, pipe_closed_handle, NULL);
declare_event_handler(output_pipe_closed, NULL, pipe_closed_handle, NULL);
