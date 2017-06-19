#ifndef __ARCH_UNIX_PIPE_H__
#define __ARCH_UNIX_PIPE_H__

#include <bathos/buffer_queue_server.h>

struct bathos_pipe;

struct arch_unix_pipe_data {
	int fd;
	struct bathos_pipe *pipe;
#ifdef CONFIG_PIPE_ASYNC_INTERFACE
	struct bathos_bqueue bqueue;
	void *buffer_area;
	struct list_head rx_queue;
	struct list_head tx_queue;
#endif
};

extern struct bathos_pipe *unix_fd_to_pipe(int fd);

#endif /* __ARCH_UNIX_PIPE_H__ */
