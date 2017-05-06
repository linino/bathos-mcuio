#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>

#include <bathos/bathos.h>
#include <bathos/errno.h>
#include <bathos/pipe.h>
#include <arch/pipe.h>

#define BATHOS_UNIX_FREE_FD -1
#define BATHOS_UNIX_RESERVED_FD -2

#ifndef MAX_BATHOS_PIPES
#define MAX_BATHOS_PIPES 32
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

static void unix_close(struct bathos_pipe *pipe)
{
	struct bathos_dev *d = pipe->dev;
	struct arch_unix_pipe_data *adata;
	if (!d)
		return;
	adata = d->priv;
	if (!adata)
		return;
	(void)close(adata->fd);
	__free_slot(adata);
	free(d);
}

static struct bathos_dev_ops unix_dev_ops = {
	.open = unix_open,
	.read = unix_read,
	.write = unix_write,
	.close = unix_close,
	/* ioctl not implemented */
};

/*
 * bathos_find_dev implementation for arch-unix
 */

struct bathos_dev *bathos_find_dev(struct bathos_pipe *p)
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
