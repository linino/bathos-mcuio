#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>

#include <bathos/bathos.h>
#include <bathos/errno.h>
#include <bathos/pipe.h>
#include <bathos/dev_ops.h>
#include <arch/pipe.h>

#ifdef CONFIG_PIPE_ASYNC_INTERFACE
#include <aio.h>
#include <signal.h>
#endif


#define BATHOS_UNIX_FREE_FD -1
#define BATHOS_UNIX_RESERVED_FD -2

#ifndef MAX_BATHOS_PIPES
#define MAX_BATHOS_PIPES 32
#endif

#ifndef PIPE_ASYNC_NBUFS
#define PIPE_ASYNC_NBUFS 16
#endif

#ifndef PIPE_ASYNC_BUFSIZE
#define PIPE_ASYNC_BUFSIZE 2048
#endif

static struct arch_unix_pipe_data priv[MAX_BATHOS_PIPES] = {
	[0 ... MAX_BATHOS_PIPES - 1] = {
		.fd = BATHOS_UNIX_FREE_FD,
	},
};

static struct arch_unix_pipe_data *__find_free_slot(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(priv); i++)
		if (priv[i].fd == BATHOS_UNIX_FREE_FD) {
			priv[i].fd = BATHOS_UNIX_RESERVED_FD;
			return &priv[i];
		}
	return NULL;
}

static void __free_slot(struct arch_unix_pipe_data *adata)
{
	adata->fd = BATHOS_UNIX_FREE_FD;
}

#ifdef CONFIG_PIPE_ASYNC_INTERFACE

declare_event(unix_async_setup);
declare_event(unix_async_done);

static int unix_async_open(struct bathos_pipe *pipe)
{
/*  https://www.ibm.com/support/knowledgecenter/en/SSLTBW_2.2.0/com.ibm.zos.v2r2.bpxbd00/raio0w.htm */
	int ret;
	struct bathos_dev *d = pipe->dev;
	struct bathos_bqueue *q = d->ops->get_bqueue(pipe);
	struct arch_unix_pipe_data *adata = d->priv;

	adata->buffer_area = malloc(PIPE_ASYNC_NBUFS*PIPE_ASYNC_BUFSIZE);
	if (!adata->buffer_area)
		return -ENOMEM;
	INIT_LIST_HEAD(&adata->rx_queue);
	INIT_LIST_HEAD(&adata->tx_queue);
	ret = bathos_bqueue_server_init(q,
					&event_name(unix_async_setup),
					&event_name(unix_async_done),
					adata->buffer_area,
					PIPE_ASYNC_NBUFS,
					PIPE_ASYNC_BUFSIZE,
					REMOTE_MAC);

	return ret;
}

static void tx_sig_handler(int sig, siginfo_t *si, void *ucontext)
{
	if (si->si_code == SI_ASYNCIO) {
		struct bathos_bdescr *b = si->si_value.sival_ptr;
		printf("I/O completion signal received\n");

		/* The corresponding ioRequest structure would be available as
		   struct ioRequest *ioReq = si->si_value.sival_ptr;
		   and the file descriptor would then be available via
		   ioReq->aiocbp->aio_fildes */
		//ARRIVATO QUI: COSA FACCIO ?????????
		//PESCO IL PUNTATORE ?
		//CHE PUNTATORE CONVIENE PASSARE ? FORSE PIU` ADATA DEL BUFFER !
		bathos_bqueue_server_buf_done(b);
	}
}

static void start_tx(struct arch_unix_pipe_data *adata)
{
	struct aiocb cb;
	struct bathos_bdescr *b;
	struct sigaction sa = {
		.sa_sigaction = tx_sig_handler,
		.sa_flags = SA_RESTART | SA_SIGINFO,
	};
	sigemptyset(&sa.sa_mask);

	if (list_empty(&adata->tx_queue)) {
		printf("%s: WARN: list is empty\n", __func__);
		return;
	}
	b = list_first_entry(&adata->tx_queue, struct bathos_bdescr, list);
	if (!b->data || !b->data_size)
		return;

	memset(&cb, 0, sizeof(cb));
	cb.aio_fildes = adata->fd;
	cb.aio_buf = b->data;
	cb.aio_nbytes = b->data_size;
	cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
	cb.aio_sigevent.sigev_signo = SIGUSR1;
	cb.aio_sigevent.sigev_value.sival_ptr = b;
	if (aio_write(&cb) < 0)
		printf("%s: WARN: aio_write() returne error\n", __func__);
	if (sigaction(SIGUSR1, &sa, b) == -1)
		perror("sigaction");
}

/*
 * This event is triggered by the client when a buffer is ready to be enqueued
 */
static void unix_async_setup_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;
	struct bathos_buffer_op *op;
	struct bathos_bqueue *q;
	struct arch_unix_pipe_data *adata;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	op = to_operation(b);
	q = b->queue;
	adata = container_of(q, struct arch_unix_pipe_data, bqueue);
	switch (op->type) {
	case NONE:
		printf("%s: WARN: operation is NONE\n", __func__);
		bathos_bqueue_server_buf_done(b);
		break;
	case SEND:
		if (!b->data) {
			printf("%s: ERR: buffers with sg lists unsupported\n",
				__func__);
			return;
		}
		/* Move buffer to tail of tx queue and start a transmission */
		list_move_tail(&b->list, &adata->tx_queue);
		start_tx(adata);
		break;
	case RECV:
		/*
		 * Move buffer to tail of rx queue (waiting for buffer to be
		 * used)
		 */
		list_move_tail(&b->list, &adata->rx_queue);
		break;
	default:
		printf("%s: ERR: invalid operation type\n", __func__);
		bathos_bqueue_server_buf_done(b);
		break;
	}
}
declare_event_handler(unix_async_setup, NULL, unix_async_setup_handler,
		      NULL);

/*
 * This is invoked when a buffer has been released by the client
 */
static void unix_async_done_handler(struct event_handler_data *ed)
{
	struct bathos_bdescr *b = ed->data;

	printf("%s\n", __func__);
	if (!b) {
		printf("%s: ERR, buffer is NULL\n", __func__);
		return;
	}
	bathos_bqueue_server_buf_done(b);
}
declare_event_handler(unix_async_done, NULL, unix_async_done_handler,
		      NULL);

#else /* !CONFIG_PIPE_ASYNC_INTERFACE */
static inline int unix_async_open(struct bathos_pipe *pipe)
{
	return -EINVAL;
}
#endif

/*
 * Pipe operations for arch-unix
 */
static int unix_open(struct bathos_pipe *pipe)
{
	int mode;
	struct bathos_dev *d = pipe->dev;
	struct arch_unix_pipe_data *adata;

	switch (pipe->mode & BATHOS_MODE_INPUT_OUTPUT) {
	case BATHOS_MODE_INPUT:
		mode = O_RDONLY;
		break;
	case BATHOS_MODE_OUTPUT:
		mode = O_WRONLY;
		break;
	case BATHOS_MODE_INPUT_OUTPUT:
		mode = O_RDWR;
		break;
	default:
		/* Invalid mode */
		return -EPERM;
	}

	if (!d)
		/* Should actually never happen */
		return -EINVAL;
	adata = d->priv;
	if (!adata)
		/* OH OH, should never happen */
		return -EINVAL;
	if (strstr(d->name, "fd:"))
		adata->fd = atoi(index(d->name, ':') + 1);
	else
		adata->fd = open(d->name, mode);
	if (adata->fd < 0)
		return -errno;

	if (pipe_mode_is_async(pipe))
		return unix_async_open(pipe);

	return 0;
}

static int unix_read(struct bathos_pipe *pipe, char *buf, int len)
{
	struct bathos_dev *d = pipe->dev;
	struct arch_unix_pipe_data *adata;
	int stat;
	if (!d)
		return -EBADF;
	adata = d->priv;
	if (!adata)
		/* OH OH, should never happen */
		return -EINVAL;
	stat = read(adata->fd, buf, len);
	return stat < 0 ? -errno : stat;
}

static int unix_write(struct bathos_pipe *pipe, const char *buf, int len)
{
	struct bathos_dev *d = pipe->dev;
	struct arch_unix_pipe_data *adata;
	int stat;
	if (!d)
		return -EBADF;
	adata = d->priv;
	if (!adata)
		return -EINVAL;
	stat = write(adata->fd, buf, len);
	return stat < 0 ? -errno : stat;
}

static int unix_close(struct bathos_pipe *pipe)
{
	struct bathos_dev *d = pipe->dev;
	struct arch_unix_pipe_data *adata;
	if (!d)
		return -ENODEV;
	adata = d->priv;
	if (!adata)
		return -ENODEV;
	(void)close(adata->fd);
	__free_slot(adata);
	free(d);
	return 0;
}

#ifdef CONFIG_PIPE_ASYNC_INTERFACE
/*
 * bathos_dev_get_bqueue implementation for arch-unix
 */
struct bathos_bqueue *unix_dev_get_bqueue(struct bathos_pipe *pipe)
{
	struct bathos_dev *d = pipe->dev;
	struct arch_unix_pipe_data *adata;
	if (!d)
		return NULL;
	adata = d->priv;
	if (!adata)
		return NULL;
	return &adata->bqueue;
}
#else
#define unix_dev_get_bqueue NULL
#endif /* CONFIG_PIPE_ASYNC_INTERFACE */

static struct bathos_dev_ops unix_dev_ops = {
	.open = unix_open,
	.read = unix_read,
	.write = unix_write,
	.close = unix_close,
	/* ioctl not implemented */
	.get_bqueue = unix_dev_get_bqueue,
};

/*
 * bathos_find_dev implementation for arch-unix
 */

struct bathos_dev *bathos_arch_find_dev(struct bathos_pipe *p)
{
	struct bathos_dev *out;
	struct arch_unix_pipe_data *adata;


	adata = __find_free_slot();
	if (!adata)
		return NULL;
	out = malloc(sizeof(*out));
	if (!out)
		return out;
	adata->pipe = p;
	out->name = p->n;
	out->ops = &unix_dev_ops;
	out->priv = adata;
	return out;
}

struct bathos_pipe *unix_fd_to_pipe(int fd)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(priv); i++) {
		if (priv[i].fd == fd)
			return priv[i].pipe;
	}
	return NULL;
}

