/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */
/* Common operations for bathos devices */

#ifndef __BATHOS_DEV_OPS_H__
#define __BATHOS_DEV_OPS_H__

#include <bathos/pipe.h>
#include <bathos/buffer.h>
#include <arch/bathos-arch.h>

/*
 * ioctl codes
 */
/* set rx high watermark */
#define DEV_IOC_RX_SET_HIGH_WATERMARK	1
/* enable rx (0 args) */
#define DEV_IOC_RX_ENABLE		2

/* Start of reserved device-specific LL ioctls */
#define DEV_IOC_PRIVATE                 0x1000

/*
 * Low level device operations
 *
 * @putc: transmit single char
 * @write: transmit buffer
 * @set_bufsize: set buffer size hook
 * @rx_disable: low level rx disable
 * @rx_enable: low level rx enable
 * @tx_disable: low level tx disable
 * @tx_enable: low level tx enable
 * @ioctl: low level ioctl
 * @async_open: invoked on open in async mode
 *
 */
struct bathos_ll_dev_ops {
	int (*putc)(void *priv, const char);
	int (*write)(void *priv, const char *, int len);
	int (*set_bufsize)(void *priv, int bufsize);
	int (*rx_disable)(void *priv);
	int (*rx_enable)(void *priv);
	int (*tx_disable)(void *priv);
	int (*tx_enable)(void *priv);
	int (*ioctl)(void *priv, struct bathos_ioctl_data *data);
	int (*async_open)(void *priv);
};

struct bathos_dev_data;

/*
 * bathos_dev_init()
 *
 * Initialize device
 *
 * @priv: low level private data (passed as first arg of low level ops)
 *
 * Returns opaque pointer to be passed to bathos_dev_push_chars() and other
 * high level device functions
 */
struct bathos_dev_data *
bathos_dev_init(const struct bathos_ll_dev_ops * PROGMEM ops, void *priv);

/*
 * bathos_dev_push_chars()
 *
 * To be invoked when chars have been received
 *
 * @dev: pointer to corresponding bathos device
 * @buf: pointer to buffer containing received chars
 * @len: length of @buf
 *
 * Returns 0 on success, errno on error.
 */
int bathos_dev_push_chars(struct bathos_dev *dev, const char *buf, int len);

#ifdef CONFIG_PIPE_ASYNC_INTERFACE

/* Get pointer to buffer queue */
struct bathos_bqueue *bathos_dev_get_bqueue(struct bathos_pipe *pipe);

void *bathos_bqueue_to_ll_priv(struct bathos_bqueue *q);

#else /* !CONFIG_PIPE_ASYNC_INTERFACE */

static inline struct bathos_bqueue *bathos_dev_get_bqueue(struct bathos_pipe *p)
{
	return NULL;
}

static inline void *bathos_bqueue_to_ll_priv(struct bathos_bqueue *q)
{
	return NULL;
}
#endif /* !CONFIG_PIPE_ASYNC_INTERFACE */

/*
 * The following functions can be used as device methods for a generic device
 */
int bathos_dev_open(struct bathos_pipe *pipe);
int bathos_dev_read(struct bathos_pipe *pipe, char *buf, int len);
int bathos_dev_write(struct bathos_pipe *pipe, const char *buf, int len);
int bathos_dev_close(struct bathos_pipe *pipe);
int bathos_dev_ioctl(struct bathos_pipe *pipe, struct bathos_ioctl_data *data);

#ifndef CONFIG_HAVE_ARCH_FIND_DEV
static inline struct bathos_dev *bathos_arch_find_dev(struct bathos_pipe *p)
{
	return NULL;
}
#else
extern struct bathos_dev *bathos_find_dev(struct bathos_pipe *);
#endif
		
#endif /* __BATHOS_DEV_OPS_H__ */

