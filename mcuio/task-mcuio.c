/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

/*
 * Very simple protocol for microprocessor <-> mcu communication
 */
#include <arch/hw.h>
#include <arch/bathos-arch.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/string.h>
#include <bathos/errno.h>
#include <tasks/mcuio.h>
#include <bathos/jiffies.h>
#include <bathos/stdio.h>
#include <generated/autoconf.h>

#include "mcuio-function.h"

struct mcuio_function_runtime mcuio_func_common_runtime;

#ifdef CONFIG_MCUIO_DEBUG
static void dump_packet(const struct mcuio_base_packet *packet)
{
	int i;
	uint8_t *ptr;
	printf("%c %c %c\n", mcuio_packet_is_error(packet) ? '!' : ' ' ,
	       mcuio_packet_is_reply(packet) ? '>' : '<',
	       mcuio_packet_is_fill_data(packet) ? '+' : ' ');
	printf("\t%u:%u.%u %s o 0x%04x\n",
	       packet->bus, packet->dev, packet->func,
	       mcuio_packet_type_to_str(packet->type),
	       packet->offset);
	printf("\tp: ");
	for (i = 0, ptr = (uint8_t *)packet; i < 2*sizeof(uint64_t); i++)
		printf("0x%02x ", ptr[i]);
	printf("\n");
}
#else
static inline void dump_packet(const struct mcuio_base_packet *packet)
{
}
#endif

declare_event(mcuio_function_request);

struct mcuio_data {
	struct bathos_pipe *input_pipe;
	struct bathos_pipe *output_pipe;
	struct mcuio_base_packet input_packet;
	int curr_len;
};

static struct mcuio_data global_data;

#if defined CONFIG_ARCH_ATMEGA
/*
 * f is a pointer to data memory
 */
static inline int __get_range(const struct mcuio_function *f,
			      int index, struct mcuio_range *out)
{
	const struct mcuio_range *r;
	r = &f->ranges[index];
	memcpy_P(out, r, sizeof(*r));
	return 0;
}

static inline struct mcuio_function *__get_function(int fn,
						    struct mcuio_function *f)
{
	struct mcuio_function *out = &mcuio_functions_start[fn];
	if (out >= mcuio_functions_end)
		return NULL;
	memcpy_P(f, out, sizeof(*f));
	return f;
}

static inline const struct mcuio_range_ops *
__get_range_ops(const struct mcuio_range *r,
		struct mcuio_range_ops *out)
{
	if (!r->ops)
		return NULL;
	memcpy_p(out, r->ops, sizeof(*out));
	return out;
}

#else /* !ARCH_ATMEGA */
static inline void __get_range(const struct mcuio_function *f,
			       int index, struct mcuio_range *out)
{
	*out = f->ranges[index];
}

static inline struct mcuio_function *__get_function(int fn,
						    struct mcuio_function *f)
{
	struct mcuio_function *out = &mcuio_functions_start[fn];
	if (out >= mcuio_functions_end)
		return NULL;
	*f = *out;
	return f;
}

static inline int __copy_byte(uint8_t *dst, const uint8_t *src)
{
	*dst = *src;
	return 0;
}

static inline int __copy_word(uint16_t *dst, const uint16_t *src)
{
	*dst = *src;
	return 0;
}

static inline int __copy_dword(uint32_t *dst, const uint32_t *src)
{
	*dst = *src;
	return 0;
}

static inline const struct mcuio_range_ops *
__get_range_ops(const struct mcuio_range *r,
		struct mcuio_range_ops *out)
{
	if (!r->ops)
		return NULL;
	memcpy(out, r->ops, sizeof(*out));
	return out;
}
#endif

static const struct mcuio_range *__lookup_range(struct mcuio_function *f,
						struct mcuio_base_packet *p,
						struct mcuio_range *r)
{
	int i, len;

	len = mcuio_packet_data_len(p);

	/* FIXME: SMARTER SEARCH */
	for (i = 0; i < f->nranges; i++) {
		uint16_t l;
		int end;

		__get_range(f, i, r);
		__copy_word(&l, (uint16_t *)r->length);
		end = r->start + l - 1;
		if ((p->offset >= r->start) &&
		    (p->offset + len - 1 <= end))
			return r;
	}
	return NULL;
}

static int __exec_request(struct mcuio_function *f,
			  const struct mcuio_range *r,
			  struct mcuio_base_packet *p)
{
	struct mcuio_range_ops ops;
	const struct mcuio_range_ops *__ops;
	mcuio_read rptr = NULL;
	mcuio_write wptr = NULL;
	int fill = mcuio_packet_is_fill_data(p);
	if (!r->ops)
		goto disabled_range;
	__ops = __get_range_ops(r, &ops);
	if (mcuio_packet_is_read(p))
		rptr = __ops->rd[(p->type & ((1 << 5) - 1)) >> 1];
	if (mcuio_packet_is_write(p))
		wptr = __ops->wr[((p->type - 1) & ((1 << 5) - 1)) >> 1];
disabled_range:
	f->runtime->to_host = *p;
	mcuio_packet_set_reply(&f->runtime->to_host);
	if (rptr)
		return rptr(r, p->offset - r->start, f->runtime->to_host.data,
			    fill);
	if (wptr)
		return wptr(r, p->offset - r->start, (const uint32_t *)p->data,
			    fill);
	return -EPERM;
}

static void __mcuio_send_to_host(struct mcuio_data *d,
				 struct mcuio_base_packet *p)
{
	int stat;
	dump_packet(p);
	stat = pipe_write(d->output_pipe, (const char *)p, sizeof(*p));
	if (stat < 0)
		printf("mcuio: output error\n");
}

static void mcuio_send_request_to_host(struct mcuio_data *d,
				       struct mcuio_base_packet *p)
{
	__mcuio_send_to_host(d, p);
}

static void mcuio_send_reply_to_host(struct mcuio_data *d,
				     struct mcuio_function *f)
{
	struct mcuio_base_packet *p = &f->runtime->to_host;

	__mcuio_send_to_host(d, p);
}

static void __mcuio_send_error_to_host(struct mcuio_data *d,
				       struct mcuio_base_packet *p,
				       int e)
{
	p->data[0] = e;
	mcuio_packet_set_error(p);
	dump_packet(p);
	pipe_write(d->output_pipe, (const char *)p, sizeof(*p));
}

static void __mcuio_send_error_to_function(struct mcuio_data *d,
					   struct mcuio_function *f,
					   int err)
{
	if (f->ops->reply)
		f->ops->reply(&d->input_packet, f, err);
}

static void mcuio_send_reply_to_function(struct mcuio_data *d,
					 struct mcuio_function *f)
{
	__mcuio_send_error_to_function(d, f, 0);
}

static void mcuio_request_received(struct mcuio_data *d,
				   struct mcuio_function *f)
{
	struct mcuio_range __r;
	const struct mcuio_range *r = NULL;
	r = __lookup_range(f, &d->input_packet, &__r);
	int stat;

	if (!r) {
		__mcuio_send_error_to_host(d, &f->runtime->to_host, EINVAL);
		return;
	}
	stat = __exec_request(f, r, &d->input_packet);
	if (stat < 0) {
		__mcuio_send_error_to_host(d, &f->runtime->to_host, -stat);
		return;
	}
	mcuio_send_reply_to_host(d, f);
}

static struct bathos_pipe *__open_input_pipe(struct mcuio_data *data)
{
	struct bathos_pipe *out = NULL;
	out = pipe_open(CONFIG_MCUIO_PIPE_INPUT_PATH, BATHOS_MODE_INPUT, data);
	if (!out)
		out = bathos_stdin;
	return out;
}

static int mcuio_init(void *arg)
{
	struct mcuio_data *data = arg;
	if (!data)
		return -1;
	data->input_pipe = __open_input_pipe(data);
	if (!data->input_pipe) {
		printf("mcuio: in pipe err\n");
		return -1;
	}
	data->output_pipe = pipe_open(CONFIG_MCUIO_PIPE_OUTPUT_PATH,
				      BATHOS_MODE_OUTPUT,
				      data);
	if (!data->output_pipe)
		data->output_pipe = bathos_stdout;
	if (!data->output_pipe) {
		printf("mcuio: out pipe err\n");
		return -1;
	}
	return 0;
}

#ifdef CONFIG_MCUIO_ALIVE_TASK
static void *mcuio_alive(void *arg)
{
	printf("mcuio ");
	arg ? printf("alive\n") : printf("dead\n");
	return arg;
}
#else
static void *mcuio_alive(void *arg)
{
	return arg;
}
#endif /* CONFIG_MCUIO_ALIVE_TASK */

static struct bathos_task __task t_mcuio = {
	.name = "mcuio", .period = 60 * HZ,
	.job = mcuio_alive, .arg = &global_data,
	.init = mcuio_init,
	.release = 3,
};

static void data_ready_handle(struct event_handler_data *ed)
{
	struct bathos_pipe *pipe;
	struct mcuio_base_packet *packet;
	int stat;
	struct mcuio_data *data = ed->priv;
	struct mcuio_function __f;
	struct mcuio_function *f;
	static int initialized;
	static unsigned long last;


	packet = &data->input_packet;

	if (!initialized)
		initialized = 1;
	else {
		if (data->curr_len) {
			if (time_after(jiffies, last + HZ/20)) {
#ifdef CONFIG_MCUIO_DEBUG
				int i;

				printf("data_ready_handle to (%d):\n\t",
				       data->curr_len);
				for (i = 0; i < data->curr_len; i++)
					printf("0x%02x ", ((char *)packet)[i]);
				printf("\n");
#endif
				data->curr_len = 0;
			}
		}
	}
	last = jiffies;

	pipe = data->input_pipe;

	stat = pipe_read(pipe, &((char *)packet)[data->curr_len],
			 sizeof(*packet) - data->curr_len);
	if (stat <= 0) {
		pr_debug("mcuio: pipe rd err (%d)\n", bathos_errno);
		if (!stat) {
			/*
			  The other end closed the pipe (or an error occurred),
			  reopen it
			*/
			pipe_close(pipe);
			data->input_pipe = __open_input_pipe(data);
		}
		data->curr_len = 0;
		return;
	}
	data->curr_len += stat;
	if (data->curr_len < sizeof(*packet))
		return;
	data->curr_len = 0;
	dump_packet(packet);
	/* HACK: fixed dev 1, ignore other destinations */
	if (packet->dev != 1)
		return;
	f = __get_function(packet->func, &__f);
	if (!f) {
		__mcuio_send_error_to_host(data, packet, -ENODEV);
		return;
	}
	mcuio_packet_is_reply(packet) ?
		mcuio_send_reply_to_function(data, f) :
		mcuio_request_received(data, f);
}

declare_event_handler_with_priv(mcuio_data_ready, NULL, data_ready_handle,
				NULL, &global_data);


static void mcuio_function_request_handle(struct event_handler_data *ed)
{
	struct mcuio_data *data = ed->priv;
	struct mcuio_base_packet *p = ed->data;
	mcuio_send_request_to_host(data, p);
}


declare_event_handler_with_priv(mcuio_function_request, NULL,
				mcuio_function_request_handle,
				NULL, &global_data);


/* Default read/write operations */

int mcuio_rdb(const struct mcuio_range *r, unsigned offset, uint32_t *__out,
	      int fill)
{
	uint8_t *out = (uint8_t *)__out;
	if (!fill) {
		if (__copy_byte(out, (uint8_t *)(r->rd_target + offset)) < 0)
			return -1;
	} else
		memcpy_p(out, r->rd_target + offset, sizeof(uint64_t));
	return fill ? sizeof(uint64_t) : sizeof(uint8_t);
}

int mcuio_rdw(const struct mcuio_range *r, unsigned offset, uint32_t *__out,
	      int fill)
{
	uint16_t *out = (uint16_t *)__out;
	if (!fill) {
		if (__copy_word(out, (uint16_t *)(r->rd_target + offset)) < 0)
			return -1;
	} else
		memcpy_p(out, r->rd_target + offset, sizeof(uint64_t));
	return fill ? sizeof(uint64_t): sizeof(uint16_t);
}

int mcuio_rddw(const struct mcuio_range *r, unsigned offset, uint32_t *out,
	       int fill)
{
	if (__copy_dword(out, (uint32_t *)(r->rd_target + offset)) < 0)
		return -1;
	if (fill)
		if (__copy_dword(&out[1], (uint32_t *)(r->rd_target + offset +
						       sizeof(uint32_t))) < 0)
			return -1;
	return fill ? sizeof(uint64_t) : sizeof(uint32_t);
}

int mcuio_rdq(const struct mcuio_range *r, unsigned offset, uint32_t *out,
	      int fill)
{
	memcpy(out, r->rd_target + offset, sizeof(uint64_t));
	return sizeof(uint64_t);
}

#ifdef ARCH_IS_HARVARD
int mcuio_rdb_ram(const struct mcuio_range *r, unsigned offset, uint32_t *__out,
		  int fill)
{
	uint8_t *out = (uint8_t *)__out;
	if (!fill) {
		*out = *(uint8_t *)(r->rd_target + offset);
	} else
		memcpy(out, r->rd_target + offset, sizeof(uint64_t));
	return fill ? sizeof(uint64_t) : sizeof(uint8_t);
}

int mcuio_rdw_ram(const struct mcuio_range *r, unsigned offset, uint32_t *__out,
		  int fill)
{
	uint16_t *out = (uint16_t *)__out;
	if (!fill) {
		*out = *(uint16_t *)(r->rd_target + offset);
	} else
		memcpy(out, r->rd_target + offset, sizeof(uint64_t));
	return fill ? sizeof(uint64_t): sizeof(uint16_t);
}

int mcuio_rddw_ram(const struct mcuio_range *r, unsigned offset, uint32_t *out,
		   int fill)
{
	*out = *(uint32_t *)(r->rd_target + offset);
	if (fill)
		out[1] = *(uint32_t *)(r->rd_target + offset +
				       sizeof(uint32_t));
	return fill ? sizeof(uint64_t) : sizeof(uint32_t);
}

int mcuio_rdq_ram(const struct mcuio_range *r, unsigned offset, uint32_t *out,
		  int fill)
{
	memcpy(out, r->rd_target + offset, sizeof(uint64_t));
	return sizeof(uint64_t);
}
#endif /* ARCH_IS_HARVARD */

int mcuio_wrb(const struct mcuio_range *r, unsigned offset,
	      const uint32_t *__in, int fill)
{
	const uint8_t *in = (const uint8_t *)__in;
	if (!fill)
		*(uint8_t *)(r->wr_target + offset) = *in;
	else
		memcpy(r->wr_target + offset, in, sizeof(uint64_t));
	return fill ? sizeof(uint64_t) : sizeof(uint8_t);
}

int mcuio_wrw(const struct mcuio_range *r, unsigned offset,
	      const uint32_t *__in, int fill)
{
	const uint16_t *in = (const uint16_t *)__in;
	if (!fill)
		*(uint16_t *)(r->wr_target + offset) = *in;
	else
		memcpy(r->wr_target + offset, in, sizeof(uint64_t));
	return fill ? sizeof(uint64_t) : sizeof(uint16_t);
}

int mcuio_wrdw(const struct mcuio_range *r, unsigned offset,
	       const uint32_t *in, int fill)
{
	*(uint32_t *)(r->wr_target + offset) = *in;
	if (fill)
		*(uint32_t *)(r->wr_target + offset + sizeof(uint32_t)) = in[1];
	return fill ? sizeof(uint64_t) : sizeof(uint32_t);
}

int mcuio_wrq(const struct mcuio_range *r, unsigned offset, const uint32_t *in,
	      int fill)
{
	memcpy(r->wr_target + offset, in, sizeof(uint64_t));
	return sizeof(uint64_t);
}

const struct mcuio_range_ops PROGMEM default_mcuio_range_ops = {
	.rd = { mcuio_rdb, mcuio_rdw, mcuio_rddw, mcuio_rdq, },
	.wr = { mcuio_wrb, mcuio_wrw, mcuio_wrdw, mcuio_wrq, },
};

const struct mcuio_range_ops PROGMEM default_mcuio_range_ram_ops = {
	.rd = { mcuio_rdb_ram, mcuio_rdw_ram, mcuio_rddw_ram, mcuio_rdq_ram, },
	.wr = { mcuio_wrb, mcuio_wrw, mcuio_wrdw, mcuio_wrq, },
};

const struct mcuio_range_ops PROGMEM default_mcuio_range_ro_ops = {
	.rd = { mcuio_rdb, mcuio_rdw, mcuio_rddw, mcuio_rdq, },
};

const struct mcuio_range_ops PROGMEM default_mcuio_range_ro_ram_ops = {
	.rd = { mcuio_rdb_ram, mcuio_rdw_ram, mcuio_rddw_ram, mcuio_rdq_ram, },
};
