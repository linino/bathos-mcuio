/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* #define DEBUG */

/*
 * bitbang i2c mcuio function, some code taken from
 * drivers/i2c/algos/i2c-algo-bit.c
 */
#include <arch/hw.h>
#include <bathos/stdio.h>
#include <bathos/gpio.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/errno.h>
#include <bathos/bitops.h>
#include <bathos/string.h>
#include <bathos/jiffies.h>
#include <bathos/delay.h>
#include <bathos/circ_buf.h>
#include <arch/gpio-bitbang-i2c.h>
#include <tasks/mcuio.h>

#include "mcuio-function.h"

#define I2C_MCUIO_BUF_MAX_SIZE 0x100
#define I2C_MCUIO_IBUF_MAX_SIZE I2C_MCUIO_BUF_MAX_SIZE
#define I2C_MCUIO_OBUF_MAX_SIZE I2C_MCUIO_BUF_MAX_SIZE

extern struct mcuio_function mcuio_bitbang_i2c;
declare_extern_event(mcuio_irq);

enum i2c_transaction_state {
	IDLE,
	START_SENT,
	ADDR_SENT,
	SENDING_DATA,
	AWAITING_OUTPUT_DATA,
	DATA_SENT,
	REPEATED_START_SENT,
	ADDR_SENT_2,
	RECEIVING_DATA,
	AWAITING_INPUT_SPACE,
	DATA_RECEIVED,
	ERROR,
};

enum i2c_event {
	I2C_GO,
	I2C_RESET,
};

static struct mcuio_i2c_data {
	int udelay;
	int timeout;
	uint16_t slave_address;
	int16_t ibuf_len;
	uint16_t obuf_len;
	uint16_t data_cnt;
	uint8_t  status;
	uint8_t  int_enable;
	uint8_t buffer[128];
	uint8_t ibuf_head;
	uint8_t ibuf_tail;
	uint8_t obuf_head;
	uint8_t obuf_tail;
	enum i2c_transaction_state state;
} i2c_data;

#define OBUF_LOW_WATERMARK  0
#define OBUF_HIGH_WATERMARK (((sizeof(i2c_data.buffer) - 1) / 8) * 8)
#define IBUF_HI_WATERMARK   (((sizeof(i2c_data.buffer) - 1) / 8) * 8)

#define MCUIO_CLASS_I2C_CONTROLLER 0x8

#if defined CONFIG_MCUIO_BITBANG_I2C_SDA
# define GPIO_SDA CONFIG_MCUIO_BITBANG_I2C_SDA
#else
# define GPIO_SDA 6
#endif

#if defined CONFIG_MCUIO_BITBANG_I2C_SCL
# define GPIO_SCL CONFIG_MCUIO_BITBANG_I2C_SCL
#else
# define GPIO_SCL 7
#endif

/*
 * Xfer data register (xfers are limited to 255 bytes)
 *
 * Byte  0   -> slave address
 * Byte  1   -> obuf length
 * Byte  2   -> ibuf length
 */
#define I2C_MCUIO_XFER_DATA	0x008
#define I2C_REG_START		I2C_MCUIO_XFER_DATA

/*
 * Status register:
 *
 * Byte 0 -> flags
 * Byte 1 -> ibuf count
 * Byte 2 -> obuf space
 */
#define I2C_MCUIO_STATUS	0x00c
#define	  TRANSACTION_OK	  0x01
#define	  OBUF_LO_WM_REACHED	  0x02
#define	  IBUF_HI_WM_REACHED	  0x04
#define	  NAK_RECEIVED		  0x80
#define	  INVALID_LEN		  0x81

#define I2C_MCUIO_CFG		0x010
#define I2C_MCUIO_BRATE		0x014
#define I2C_MCUIO_CMD		0x018
#define	  START_TRANSACTION	  0x1
#define	  INTEN			  0x2
#define I2C_REG_END		0x03f

#define I2C_MCUIO_BUF_SIZE	0x020

#define I2C_MCUIO_OBUF		0x040
#define I2C_MCUIO_IBUF		(I2C_MCUIO_OBUF + I2C_MCUIO_OBUF_MAX_SIZE)


static const struct mcuio_func_descriptor PROGMEM i2c_bitbang_descr = {
	.device = CONFIG_MCUIO_BITBANG_I2C_DEVICE,
	.vendor = CONFIG_MCUIO_BITBANG_I2C_VENDOR,
	.rev = 0,
	/* I2C controller class */
	.class = MCUIO_CLASS_I2C_CONTROLLER,
};

static const unsigned int PROGMEM i2c_bitbang_descr_length =
    sizeof(i2c_bitbang_descr);

static const unsigned int PROGMEM i2c_bitbang_registers_length =
	I2C_REG_END - I2C_REG_START + 1;

static const unsigned int PROGMEM i2c_bitbang_obuf_length =
	sizeof(i2c_data.buffer);

static const unsigned int PROGMEM i2c_bitbang_ibuf_length =
	sizeof(i2c_data.buffer);

declare_event(i2c_transaction);

#ifndef i2c_udelay
#define i2c_udelay(a) udelay(a)
#endif

#ifndef setsda
static inline void setsda(int v)
{
	gpio_dir(GPIO_SDA, 1, v);
	if (v)
		gpio_dir(GPIO_SDA, 0, 1);
}
#endif

#ifndef setscl
static inline void setscl(int v)
{
	gpio_dir(GPIO_SCL, 1, v);
	if (v)
		gpio_dir(GPIO_SCL, 0, 1);
}
#endif

#ifndef getscl
static inline int getscl(void)
{
	return gpio_get(GPIO_SCL);
}
#endif

#ifndef getsda
static inline int getsda(void)
{
	int ret = gpio_get(GPIO_SDA);
	return ret;
}
#endif

#ifndef sdalo
static inline void sdalo(void)
{
	setsda(0);
	i2c_udelay((i2c_data.udelay + 1) / 2);
}
#endif

#ifndef sdahi
static inline void sdahi(void)
{
	setsda(1);
	i2c_udelay((i2c_data.udelay + 1) / 2);
}
#endif

#ifndef scllo
static inline void scllo(void)
{
	setscl(0);
	i2c_udelay(i2c_data.udelay / 2);
}
#endif

#ifndef sclhi
static inline int sclhi(void)
{
	unsigned long start;
	setscl(1);
	start = jiffies;
	while (!getscl()) {
		/* Wait for slave to leave scl (clock stretching) */
		if (time_after(jiffies, start + i2c_data.timeout))
			return -ETIMEDOUT;
	}
	i2c_udelay(i2c_data.udelay);
	return 0;
}
#endif


static void __i2c_bitbang_send_start(void)
{
	setsda(0);
	i2c_udelay(i2c_data.udelay);
	scllo();
}

static void __i2c_bitbang_send_repstart(void)
{
	sdahi();
	sclhi();
	setsda(0);
	i2c_udelay(i2c_data.udelay);
	scllo();
}

static void __i2c_bitbang_send_stop(void)
{
	sdalo();
	sclhi();
	setsda(1);
	i2c_udelay(i2c_data.udelay);
}

static int __i2c_bitbang_send_byte(uint8_t c)
{
	int i;
	int sb;
	int ack;

	/* assert: scl is low */
	for (i = 7; i >= 0; i--) {
		sb = (c >> i) & 1;
		setsda(sb);
		i2c_udelay((i2c_data.udelay + 1) / 2);
		if (sclhi() < 0)
			return -ETIMEDOUT;
		scllo();
	}
	sdahi();
	if (sclhi() < 0)
		return -ETIMEDOUT;

	/* read ack: SDA should be pulled down by slave, or it may
	 * NAK (usually to report problems with the data we wrote).
	 */
	ack = !getsda();	/* ack: sda is pulled low -> success */
	scllo();
	return ack ? 0 : -1;
	/* assert: scl is low (sda undef) */
}

static int __i2c_bitbang_recv_byte(uint8_t *out)
{
	/* read byte via i2c port, without start/stop sequence	*/
	/* acknowledge is sent in i2c_read.			*/
	int i;
	*out = 0;

	/* assert: scl is low */
	sdahi();
	for (i = 0; i < 8; i++) {
		if (sclhi() < 0)
			return -ETIMEDOUT;
		*out *= 2;
		if (getsda())
			*out |= 0x01;
		setscl(0);
		i2c_udelay(i == 7 ? i2c_data.udelay / 2 : i2c_data.udelay);
	}
	/* assert: scl is low */
	return 0;
}

static int __i2c_bitbang_send_slave_addr(int force_rd)
{
	u8 a = i2c_data.slave_address;
	return __i2c_bitbang_send_byte(a | (force_rd ? 1 : 0));
}

static int __i2c_bitbang_send_acknak(int is_ack)
{
	if (is_ack)
		setsda(0);
	i2c_udelay((i2c_data.udelay + 1) / 2);
	if (sclhi() < 0)
		return -ETIMEDOUT;
	scllo();
	return 0;
}

static int __trigger_go_event(void)
{
	pr_debug("triggering evt I2C_GO\n");
	return trigger_event(&event_name(i2c_transaction), (void *)I2C_GO);
}

/*
 * Returns 0 when GO event handler must be left to allow for more events
 * to be handled, !0 otherwise
 */
static int __i2c_bitbang_next_state(enum i2c_transaction_state s)
{
	i2c_data.state = s;

	pr_debug("__i2c_bitbang_next_state %d\n", s);
	if (s == IDLE || s == AWAITING_OUTPUT_DATA ||
	    s == AWAITING_INPUT_SPACE)
		return 0;

	return 1;
}

static void __i2c_bitbang_trigger_irq(void)
{
	static struct mcuio_function_irq_data idata;
	int f;

	f = &mcuio_bitbang_i2c - mcuio_functions_start;
	idata.func = f;
	idata.active = 1;
	pr_debug("__i2c_bitbang_trigger_irq()\n");
	if (trigger_event(&event_name(mcuio_irq), &idata))
		printf("i2c trg irq: evt err\n");
}

static void __i2c_bitbang_end_transaction(uint8_t s)
{
	/* send stop */
	pr_debug("__i2c_bitbang_end_transaction()\n");
	__i2c_bitbang_send_stop();

	i2c_data.status = s;
	i2c_data.state = s != TRANSACTION_OK ? ERROR : IDLE;
	__i2c_bitbang_trigger_irq();
}

static void __i2c_handle_reset(void)
{
	pr_debug("__i2c_handle_reset s%d\n", i2c_data.state);
	i2c_data.status = 0;
	__i2c_bitbang_next_state(IDLE);
}

static int __i2c_handle_go(void)
{
	i2c_data.timeout = HZ;
	i2c_data.udelay = 1;
	int ret = 0;

	pr_debug("__i2c_handle_go, s%d\n", i2c_data.state);
	switch (i2c_data.state) {
	case IDLE:
		pr_debug("sending start\n");
		/* Send start condition */
		__i2c_bitbang_send_start();
		/* -> START_SENT */
		pr_debug("start sent\n");
		ret = __i2c_bitbang_next_state(START_SENT);
		break;
	case START_SENT:
	{
		int force_rd = i2c_data.ibuf_len && !i2c_data.obuf_len;
		pr_debug("sending slave address\n");
		/* Send slave address + w bit */
		if (__i2c_bitbang_send_slave_addr(force_rd) < 0) {
			pr_debug("error sending slave address\n");
			__i2c_bitbang_end_transaction(NAK_RECEIVED);
			break;
		}
		/* -> ADDR_SENT */
		pr_debug("address send\n");
		ret = __i2c_bitbang_next_state(ADDR_SENT);
		break;
	}
	case ADDR_SENT:
		if (!i2c_data.obuf_len) {
			pr_debug("no out data, switching to DATA_SENT state\n");
			/* No data */
			ret = __i2c_bitbang_next_state(DATA_SENT);
			break;
		}
		/* Reset byte counter */
		i2c_data.data_cnt = 0;
		/* -> SENDING_DATA */
		ret = __i2c_bitbang_next_state(SENDING_DATA);
		break;
	case SENDING_DATA:
	{
		int done, count, togo;
		uint8_t c = i2c_data.buffer[i2c_data.obuf_tail];

		pr_debug("SENDING_DATA state, c = %u\n", c);
		/* Send a single byte */
		if (__i2c_bitbang_send_byte(c) < 0) {
			pr_debug("NAK received sending byte\n");
			/* Throw away remaining data */
			i2c_data.obuf_tail = i2c_data.obuf_head;
			__i2c_bitbang_end_transaction(NAK_RECEIVED);
			break;
		}
		i2c_data.data_cnt++;
		i2c_data.obuf_tail = (i2c_data.obuf_tail + 1) &
			(sizeof(i2c_data.buffer) - 1);
		/* Set next state */
		done = i2c_data.data_cnt == i2c_data.obuf_len;
		if (done) {
			/* equalize tail to head: the host always writes
			   dwords, maybe 1 to 3 bytes are in eccess
			*/
			i2c_data.obuf_tail = i2c_data.obuf_head;
			ret = __i2c_bitbang_next_state(DATA_SENT);
			break;
		}
		count = CIRC_CNT(i2c_data.obuf_head, i2c_data.obuf_tail,
				 sizeof(i2c_data.buffer));
		togo = i2c_data.obuf_len - i2c_data.data_cnt;
		pr_debug("count = %d\n", count);
		if (count == OBUF_LOW_WATERMARK && count < togo) {
			pr_debug("hit lower watermark\n");
			/* Trigger out low watermark interrupt */
			i2c_data.status = OBUF_LO_WM_REACHED;
			__i2c_bitbang_trigger_irq();
		}

		ret = __i2c_bitbang_next_state(count ?
					       SENDING_DATA :
					       AWAITING_OUTPUT_DATA);
		break;
	}
	case DATA_SENT:
		if (!i2c_data.ibuf_len) {
			pr_debug("DATA_SENT state, transaction ok\n");
			__i2c_bitbang_end_transaction(TRANSACTION_OK);
			break;
		}
		if (!i2c_data.obuf_len) {
			/* Read data without writing cmd first */
			pr_debug("read data without sending cmd first\n");
			/* Reset counter */
			i2c_data.data_cnt = i2c_data.ibuf_len == -1 ? -1 : 0;
			ret = __i2c_bitbang_next_state(RECEIVING_DATA);
		} else {
			/* Output data sent, send repeated start */
			pr_debug("sending repeated start\n");
			__i2c_bitbang_send_repstart();
			ret = __i2c_bitbang_next_state(REPEATED_START_SENT);
		}
		/* Reset counter */
		i2c_data.data_cnt = 0;
		break;
	case REPEATED_START_SENT:
		/* Send slave address + r bit */
		pr_debug("REPEATED_START_SENT state\n");
		if (__i2c_bitbang_send_slave_addr(1) < 0) {
			pr_debug("NAK received\n");
			__i2c_bitbang_end_transaction(NAK_RECEIVED);
			break;;
		}
		/* -> ADDR_SENT_2 */
		pr_debug("address + r sent\n");
		ret = __i2c_bitbang_next_state(ADDR_SENT_2);
		break;
	case ADDR_SENT_2:
		pr_debug("ADDR_SENT_2 state\n");
		/* Reset counter */
		i2c_data.data_cnt = i2c_data.ibuf_len == -1 ? -1 : 0;
		pr_debug("i2c_data.data_cnt reset to %u\n", i2c_data.data_cnt);
		/* -> RECEIVING_DATA */
		ret = __i2c_bitbang_next_state(RECEIVING_DATA);
		break;
	case RECEIVING_DATA:
	{
		uint8_t c;
		int done, space;
		/* Read byte */
		pr_debug("RECEIVING_DATA state\n");
		__i2c_bitbang_recv_byte(&c);
		i2c_data.buffer[i2c_data.ibuf_head] = c;
		pr_debug("c = 0x%02x\n", c);
		if (i2c_data.data_cnt == -1) {
			if (c > CIRC_SPACE(i2c_data.ibuf_head,
					   i2c_data.ibuf_tail,
					   sizeof(i2c_data.buffer))) {
				pr_debug("SMBUS IS TOO BIG\n");
				__i2c_bitbang_end_transaction(INVALID_LEN);
				break;
			}
			/*
			 * smbus read block, the device shall tell us about
			 * block lenght (max 32)
			 */
			i2c_data.data_cnt = 0;
			i2c_data.ibuf_len = c;
		}
		i2c_data.data_cnt++;
		done = i2c_data.data_cnt == i2c_data.ibuf_len;
		__i2c_bitbang_send_acknak(!done);
		i2c_data.ibuf_head = (i2c_data.ibuf_head + 1) &
			(sizeof(i2c_data.buffer) - 1);
		if (done) {
			pr_debug("done\n");
			ret = __i2c_bitbang_next_state(DATA_RECEIVED);
			break;
		}
		space = CIRC_SPACE(i2c_data.ibuf_head,
				   i2c_data.ibuf_tail,
				   sizeof(i2c_data.buffer));
		pr_debug("space = %d\n", space);
		if (space == IBUF_HI_WATERMARK) {
			pr_debug("triggering irq\n");
			i2c_data.status = IBUF_HI_WM_REACHED;
			__i2c_bitbang_trigger_irq();
		}

		ret = __i2c_bitbang_next_state(space ?
					       RECEIVING_DATA :
					       AWAITING_INPUT_SPACE);
		break;
	}
	case DATA_RECEIVED:
		/* All done, end transaction */
		pr_debug("DATA_RECEIVED state, tranaction ok\n");
		__i2c_bitbang_end_transaction(TRANSACTION_OK);
		break;
	case AWAITING_OUTPUT_DATA:
	case AWAITING_INPUT_SPACE:
	case ERROR:
		/* Error state, ignore go events */
		break;
	}
	return ret;
}

static void __i2c_bitbang_do_transaction(struct event_handler_data *ed)
{
	enum i2c_event evt = (enum i2c_event)ed->data;

	switch (evt) {
	case I2C_RESET:
		__i2c_handle_reset();
		break;
	case I2C_GO:
		while (__i2c_handle_go());
		break;
	default:
		printf("i2c do trans %d\n", evt);
		break;
	}
}

declare_event_handler(i2c_transaction, NULL,
		      __i2c_bitbang_do_transaction, NULL);

static int __i2c_bitbang_start_transaction(void)
{
	pr_debug("__i2c_bitbang_start_transaction s%d\n", i2c_data.status);
	return trigger_event(&event_name(i2c_transaction), (void *)I2C_GO);
}

static int i2c_bitbang_registers_rddw(const struct mcuio_range *r,
				      unsigned offset,
				      uint32_t *__out, int fill)
{
	unsigned i, size = fill ? sizeof(uint64_t) : sizeof(uint32_t);
	uint32_t *out;
	static struct mcuio_function_irq_data idata;

	for (i = 0; i < size; i += sizeof(uint32_t)) {
		out = &__out[i / sizeof(uint32_t)];
		switch (i + offset + r->start) {
		case I2C_MCUIO_XFER_DATA:
			/* Slave address */
			*out = mcuio_htonl(i2c_data.slave_address |
					   ((uint32_t)i2c_data.obuf_len << 8) |
					   ((uint32_t)i2c_data.ibuf_len << 16));
			break;
		case I2C_MCUIO_STATUS:
			/* Status */
		{
			uint32_t icount, ospace;

			icount = CIRC_CNT(i2c_data.ibuf_head,
					  i2c_data.ibuf_tail,
					  sizeof(i2c_data.buffer));
			ospace = CIRC_SPACE(i2c_data.obuf_head,
					    i2c_data.obuf_tail,
					    sizeof(i2c_data.buffer));
			*out = mcuio_htonl(i2c_data.status |
					   (icount << 8) |
					   (ospace << 16));
			if (i2c_data.status) {
				/* Automatically clear interrupt */
				int f = &mcuio_bitbang_i2c -
					mcuio_functions_start;
				idata.func = f;
				idata.active = 0;
				if (trigger_event(&event_name(mcuio_irq),
						  &idata))
					printf("i2c reg rddw: evt err\n");
				if (i2c_data.state != AWAITING_OUTPUT_DATA &&
				    i2c_data.state != AWAITING_INPUT_SPACE &&
				    i2c_data.state != SENDING_DATA &&
				    i2c_data.state != RECEIVING_DATA) {
					trigger_event(&event_name(
							      i2c_transaction),
						      (void *)I2C_RESET);
				}
			}
			break;
		}
		case I2C_MCUIO_CFG:
		case I2C_MCUIO_BRATE:
			/* unsupported at the moment */
			*out = 0;
			break;
		case I2C_MCUIO_CMD:
			/* command always reads back 0 */
			*out = 0;
			break;
		case I2C_MCUIO_BUF_SIZE:
			/* Buffer size */
			*out = mcuio_ntohl(sizeof(i2c_data.buffer));
			break;
		default:
			return -EPERM;
		}
	}
	return size;
}

static int i2c_bitbang_registers_wrdw(const struct mcuio_range *r,
				      unsigned offset,
				      const uint32_t *__in, int fill)
{
	unsigned i, size = fill ? sizeof(uint64_t) : sizeof(uint32_t);
	for (i = 0; i < size; i += sizeof(uint32_t)) {
		uint32_t in = mcuio_ntohl(__in[i / sizeof(uint32_t)]);
		switch (i + offset + r->start) {
		case I2C_MCUIO_XFER_DATA:
		{
			/* Transfer data */
			i2c_data.slave_address = in & 0xff;
			i2c_data.obuf_len = (in >> 8) & 0xfff;
			i2c_data.ibuf_len = (in >> 20) & 0xfff;
			pr_debug("XFER_DATA: a = 0x%02x ol = %u il = %u\n",
				 i2c_data.slave_address, i2c_data.obuf_len,
				 i2c_data.ibuf_len);
			break;
		}
		case I2C_MCUIO_STATUS:
			return -EPERM;
		case I2C_MCUIO_CFG:
		case I2C_MCUIO_BRATE:
			/* unsupported at the moment */
			break;
		case I2C_MCUIO_CMD:
		{
			int stat;
			pr_debug("i2c_bitbang_registers_wrdw c %u\n", in);
			if (in & START_TRANSACTION) {
				stat = __i2c_bitbang_start_transaction();
				if (stat < 0)
					return stat;
			}
			i2c_data.int_enable = (in & INTEN);
			break;
		}
		case I2C_MCUIO_BUF_SIZE:
			/* Buffer size, cannot be written */
			return -EPERM;
		default:
			return -EPERM;
		}
	}
	return size;
}


const struct mcuio_range_ops PROGMEM i2c_bitbang_registers_rw_ops = {
	.rd = { NULL, NULL, i2c_bitbang_registers_rddw, NULL, },
	.wr = { NULL, NULL, i2c_bitbang_registers_wrdw, NULL, },
};

int i2c_bitbang_obuf_wrb(const struct mcuio_range *r, unsigned offset,
			 const uint32_t *__in, int fill)
{
	int space_to_end, space, sz, l, written = 0;
	const uint8_t *in = (const uint8_t *)__in;

	sz = fill ? sizeof(uint64_t) : sizeof(uint8_t);
	space = CIRC_SPACE(i2c_data.obuf_head, i2c_data.obuf_tail,
			   sizeof(i2c_data.buffer));
	if (sz > space)
		return -EINVAL;
	space_to_end = CIRC_SPACE_TO_END(i2c_data.obuf_head,
					 i2c_data.obuf_tail,
					 sizeof(i2c_data.buffer));
	l = min(sz, space_to_end);
	memcpy(&i2c_data.buffer[i2c_data.obuf_head], in, l);
	sz -= l;
	space -= l;
	written += l;
	in += l;
	if (sz > 0) {
		l = min(sz, space);
		memcpy(&i2c_data.buffer[0], in, l);
		written += l;
		space -= l;
	}
	i2c_data.obuf_head = (i2c_data.obuf_head + written) &
		(sizeof(i2c_data.buffer) - 1);
	if (i2c_data.state == AWAITING_OUTPUT_DATA) {
		int togo = i2c_data.obuf_len - i2c_data.data_cnt;
		int have = CIRC_CNT(i2c_data.obuf_head,
				    i2c_data.obuf_tail,
				    sizeof(i2c_data.buffer));
		if (have >= min(togo, OBUF_HIGH_WATERMARK)) {
			__i2c_bitbang_next_state(SENDING_DATA);
			__trigger_go_event();
		}
	}
	return sz;
}

const struct mcuio_range_ops PROGMEM i2c_bitbang_obuf_wr_ops = {
	/* Can't read the output buffer anyway */
	.rd = { NULL, NULL, NULL, NULL, },
	/* Only byte access is allowed */
	.wr = { i2c_bitbang_obuf_wrb, NULL, NULL, NULL, },
};

int i2c_bitbang_ibuf_rdb(const struct mcuio_range *r, unsigned offset,
			 uint32_t *__out, int fill)
{
	int count_to_end, count, sz, l, read = 0;
	uint8_t *out = (uint8_t *)__out;

	sz = fill ? sizeof(uint64_t) : sizeof(uint8_t);
	count = CIRC_CNT(i2c_data.ibuf_head, i2c_data.ibuf_tail,
			 sizeof(i2c_data.buffer));
	if (sz > count)
		return -EINVAL;
	count_to_end = CIRC_CNT_TO_END(i2c_data.ibuf_head,
				       i2c_data.ibuf_tail,
				       sizeof(i2c_data.buffer));
	l = min(sz, count_to_end);
	memcpy(out, &i2c_data.buffer[i2c_data.ibuf_tail], l);
	sz -= l;
	count -= l;
	read += l;
	out += l;
	if (sz > 0) {
		l = min(sz, count);
		memcpy(out, &i2c_data.buffer[0], l);
		read += l;
		count -= l;
	}
	i2c_data.ibuf_tail = (i2c_data.ibuf_tail + read) &
		(sizeof(i2c_data.buffer) - 1);
	if (i2c_data.state == AWAITING_INPUT_SPACE) {
		__i2c_bitbang_next_state(RECEIVING_DATA);
		__trigger_go_event();
	}
	return sz;
}

const struct mcuio_range_ops PROGMEM i2c_bitbang_ibuf_rd_ops = {
	/* Only byte access is allowed */
	.rd = { i2c_bitbang_ibuf_rdb, NULL, NULL, NULL, },
	/* Can't write the input buffer anyway */
	.wr = { NULL, NULL, NULL, NULL, },
};

static const struct mcuio_range PROGMEM i2c_bitbang_ranges[] = {
	/* i2c bitbang func descriptor */
	{
		.start = 0,
		.length = &i2c_bitbang_descr_length,
		.rd_target = &i2c_bitbang_descr,
		.ops = &default_mcuio_range_ro_ops,
	},
	/* dwords 0x8 .. 0x2b: registers, read/write */
	{
		.start = 8,
		.length = &i2c_bitbang_registers_length,
		.rd_target = NULL,
		.ops = &i2c_bitbang_registers_rw_ops,
	},
	/* dwords 0x40 .. 0x13f, output buffer, write only */
	{
		.start = 0x40,
		.length = &i2c_bitbang_obuf_length,
		.wr_target = i2c_data.buffer,
		.ops = &i2c_bitbang_obuf_wr_ops,
	},
	/* dwords 0x140 .. 0x240, input buffer, read only */
	{
		.start = 0x140,
		.length = &i2c_bitbang_ibuf_length,
		.rd_target = i2c_data.buffer,
		.ops = &i2c_bitbang_ibuf_rd_ops,
	},
};


declare_mcuio_function(mcuio_bitbang_i2c, i2c_bitbang_ranges, NULL, NULL,
		       &mcuio_func_common_runtime);
