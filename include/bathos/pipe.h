/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef __PIPE_H__
#define __PIPE_H__

#include <bathos/event.h>
#include <bathos/bathos.h>
#include <bathos/buffer.h>
#include <linux/list.h>


#define BATHOS_MODE_INPUT  1
#define BATHOS_MODE_OUTPUT 2
#define BATHOS_MODE_ASYNC  4
#define BATHOS_MODE_INPUT_OUTPUT (BATHOS_MODE_INPUT|BATHOS_MODE_OUTPUT)

struct bathos_pipe;
struct bathos_dev_data;

struct bathos_ioctl_data {
	int code;
	void *data;
};

struct bathos_dev_ops {
	int (*open)(struct bathos_pipe *);
	int (*read)(struct bathos_pipe *, char *buf, int len);
	int (*write)(struct bathos_pipe *, const char *buf, int len);
	/* Invoked on every pipe close */
	int (*on_pipe_close)(struct bathos_pipe *);
	/* Invoked on last pipe close */
	int (*close)(struct bathos_pipe *);
	int (*ioctl)(struct bathos_pipe *, struct bathos_ioctl_data *);
#ifdef CONFIG_PIPE_ASYNC_INTERFACE
	struct bathos_bqueue *(*get_bqueue)(struct bathos_pipe *);
#endif
};

struct bathos_dev {
	const char *name;
	const struct bathos_dev_ops * PROGMEM ops;
	void *priv;
	/* private field used by arch-unix only at present */
	void *arch_priv;
	/* Low level driver's private data */
	void *ll_priv;
	/* Low level operations */
	const struct bathos_ll_dev_ops * PROGMEM ll_ops;
	const void *platform_data;
	struct list_head pipes;
};

struct bathos_pipe {
	const char *n;
	int mode;
	void *data;
	const struct event *input_ready_event;
	const struct event *output_ready_event;
	const struct event *buffer_available_event;
	const struct event *buffer_processed_event;
	struct bathos_dev *dev;
	/* list of pipes related to the same device */
	struct list_head list;
	/* Device related data, relevant to current pipe */
	struct bathos_dev_data *dev_data;
};

/*
 * pipe_open()
 * @n : pipe name (can be NULL, in which case a new descriptor is just returned)
 * @data : data passed on to input_ready/output_ready event callback
 */
struct bathos_pipe *pipe_open(const char *n, int mode, void *data);
int pipe_close(struct bathos_pipe *);
int pipe_read(struct bathos_pipe *, char *buf, int len);
int pipe_write(struct bathos_pipe *, const char *buf, int len);
int pipe_ioctl(struct bathos_pipe *, struct bathos_ioctl_data *data);

void __pipe_dev_trigger_event(struct bathos_dev *dev, const struct event *evt,
			      int immediate);
void __pipe_trigger_event(struct bathos_pipe *p, const struct event *evt,
			  int immediate);

#ifdef CONFIG_PIPE_ASYNC_INTERFACE

extern struct bathos_bdescr *pipe_async_get_buf_dir(struct bathos_pipe *p,
						    enum buffer_dir dir);

static inline struct bathos_bdescr *pipe_async_get_buf(struct bathos_pipe *p)
{
	return pipe_async_get_buf_dir(p, ANY);
}

int pipe_async_start(struct bathos_pipe *);

#else /* !CONFIG_PIPE_ASYNC_INTERFACE */

static inline struct bathos_bdescr *
pipe_async_get_buf_dir(struct bathos_pipe *p, enum buffer_dir dir)
{
	return NULL;
}

static inline struct bathos_bdescr *pipe_async_get_buf(struct bathos_pipe *p)
{
	return NULL;
}

static inline int pipe_async_start(struct bathos_pipe *p)
{
	return -ENOSYS;
}
#endif /* !CONFIG_PIPE_ASYNC_INTERFACE */

static inline void
pipe_dev_trigger_event(struct bathos_dev *dev, const struct event *evt)
{
	__pipe_dev_trigger_event(dev, evt, 0);
}

static inline void
pipe_dev_trigger_event_immediate(struct bathos_dev *dev,
				 const struct event *evt)
{
	__pipe_dev_trigger_event(dev, evt, 1);
}

static inline void
pipe_trigger_event(struct bathos_pipe *p, const struct event *evt)
{
	__pipe_trigger_event(p, evt, 0);
}

static inline void
pipe_trigger_event_immediate(struct bathos_pipe *p, const struct event *evt)
{
	__pipe_trigger_event(p, evt, 1);
}


static inline void pipe_remap_input_ready_event(struct bathos_pipe *pipe,
						const struct event * PROGMEM e)
{
	pipe->input_ready_event = e;
}

static inline void pipe_remap_output_ready_event(struct bathos_pipe *pipe,
						 const struct event * PROGMEM e)
{
	pipe->output_ready_event = e;
}

static inline void
pipe_remap_buffer_available_event(struct bathos_pipe *pipe,
				  const struct event * PROGMEM e)
{
	pipe->buffer_available_event = e;
}

static inline void
pipe_remap_buffer_processed_event(struct bathos_pipe *pipe,
				  const struct event * PROGMEM e)
{
	pipe->buffer_processed_event = e;
}

static inline int pipe_mode_is_async(struct bathos_pipe *pipe)
{
	return pipe->mode & BATHOS_MODE_ASYNC;
}

declare_extern_event(input_pipe_opened);
declare_extern_event(output_pipe_opened);
declare_extern_event(pipe_input_ready);
declare_extern_event(pipe_output_ready);
declare_extern_event(pipe_error);
declare_extern_event(pipe_buffer_available);
declare_extern_event(pipe_buffer_processed);

#endif /* __PIPE_H__ */
