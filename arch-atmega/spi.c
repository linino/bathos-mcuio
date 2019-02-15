/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Aurelio Colosimo <aurelio@aureliocolosimo.it>
 */

/*
 * SPI driver for bathos
 */

#include <bathos/jiffies.h>
#include <bathos/pipe.h>
#include <bathos/circ_buf.h>
#include <bathos/string.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/types.h>
#include <bathos/stdio.h>
#include <arch/hw.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <bathos/allocator.h>

#define SPI_BUF_SIZE 64

struct spi_data {
	struct circ_buf cbufrx;
	char *bufrx;
	int overrun;

	struct circ_buf cbuftx;
	char *buftx;
};

struct spi_data spi_data;

struct bathos_dev __spi_dev;

static int spi_init(void)
{
	/* Enable pull-up on SS signal */
	PORTB |= 0x1;
	/* MISO output (PB3) */
	DDRB |= (1 << 3);
	return 0;
}

rom_initcall(spi_init);

static int spi_open(struct bathos_pipe *pipe)
{
	/* Enable SPI (in slave mode) and its interrupt */
	SPCR = (1 << SPE);
	SPCR |= (1 << SPIE);
	spi_data.bufrx = bathos_alloc_buffer(SPI_BUF_SIZE);
	spi_data.buftx = bathos_alloc_buffer(SPI_BUF_SIZE);
	return 0;
}

static int spi_close(struct bathos_pipe *pipe)
{
	SPCR &= ~(1 << SPIE);
	SPCR &= ~(1 << SPE);
	bathos_free_buffer(spi_data.bufrx, SPI_BUF_SIZE);
	bathos_free_buffer(spi_data.buftx, SPI_BUF_SIZE);
	return 0;
}

static int spi_read(struct bathos_pipe *pipe, char *buf, int len)
{
	struct spi_data *data = &spi_data;
	int flags;
	int l;

	interrupt_disable(flags);
	l = min(len, CIRC_CNT_TO_END(data->cbufrx.head, data->cbufrx.tail,
				     SPI_BUF_SIZE));

	if (!l) {
		interrupt_restore(flags);
		return -EAGAIN;
	}

	memcpy(buf, &data->bufrx[data->cbufrx.tail], l);
	data->cbufrx.tail = (data->cbufrx.tail + l) & (SPI_BUF_SIZE - 1);
	data->overrun = 0;

	if (CIRC_CNT(data->cbufrx.head, data->cbufrx.tail, SPI_BUF_SIZE))
		pipe_dev_trigger_event(&__spi_dev, &evt_pipe_input_ready);

	interrupt_restore(flags);
	return l;
}

static int spi_write(struct bathos_pipe *pipe, const char *buf, int len)
{
	struct spi_data *data = &spi_data;
	int i;
	int s = CIRC_SPACE(data->cbuftx.head, data->cbuftx.tail,
		SPI_BUF_SIZE);
	int l = min(len, s);

	if (!l)
		return -EAGAIN;

	for (i = 0; i < l; i++) {
		data->buftx[data->cbuftx.head] = buf[i];
		data->cbuftx.head = (data->cbuftx.head + 1)
			& (SPI_BUF_SIZE - 1);
	}

	return l;
}

declare_extern_event(spi_pipe_input_ready);

ISR(SPI_STC_vect, __attribute__((section(".text.ISR"))))
{
	u8 c = SPDR;
	struct spi_data *data = &spi_data;
	int cnt_prev;

	if (c == 0x05) { /* enquiry */
		if (CIRC_CNT(data->cbuftx.head, data->cbuftx.tail,
		SPI_BUF_SIZE)) {
			SPDR = data->buftx[data->cbuftx.tail];
			data->cbuftx.tail = (data->cbuftx.tail + 1)
				& (SPI_BUF_SIZE - 1);
		}
		else
			SPDR = 0x00;
	}

	else {
		SPDR = 0x00;

		if (c == 0x00)
			return;

		cnt_prev = CIRC_CNT(data->cbufrx.head, data->cbufrx.tail,
				    SPI_BUF_SIZE);
		if (!CIRC_SPACE(data->cbufrx.head, data->cbufrx.tail,
			SPI_BUF_SIZE))
			data->overrun = 1;

		if (!data->overrun) {
			data->bufrx[data->cbufrx.head] = c;
			data->cbufrx.head = (data->cbufrx.head + 1)
				& (SPI_BUF_SIZE - 1);
		}
		if (!cnt_prev)
			pipe_dev_trigger_event(&__spi_dev,
					       &evt_pipe_input_ready);
	}

}

static const struct bathos_dev_ops PROGMEM spi_dev_ops = {
	.open = spi_open,
	.read = spi_read,
	.write = spi_write,
	.close = spi_close,
	/* ioctl not implemented */
};

struct bathos_dev __spi_dev __attribute__((section(".bathos_devices"),
					    aligned(2))) = {
	.name = "avr-spi",
	.ops = &spi_dev_ops,
};
