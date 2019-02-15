/*
 * enc28j60 driver for ssw, Alessandro Rubini 2010, GNu GPL V2
 * Based on previous work by Guido Socher, who based his own on Pascal Stang's
 */
#include <bathos/bathos.h>
#include <bathos/delay.h>
#include <bathos/enc28j60.h>
#include <bathos/delay.h>
#include <bathos/spi.h>
#include "enc28j60.h"

#ifndef DEBUG_ENC28
#define DEBUG_ENC28 0
#endif

/* wr_op and rd_op do not set the bank, assuming it is already set */
static int __enc28_wr_op(struct enc28_dev *dev, int op, int addr, int datum)
{
	unsigned char b[2];
	struct spi_obuf sob = {.len = 2, .buf = b};

	b[0] = op | (addr & ENC28_MASK_ADDR);
	b[1] = datum;

	return spi_write(dev->spi_dev, SPI_F_DEFAULT, &sob);
}

static int __enc28_rd_op(struct enc28_dev *dev, int op, int addr)
{
	union {
		struct spi_ibuf i;
		struct spi_obuf o;
	} sb;
	unsigned char b[3] = {0xff, 0xff, 0xff};
	int len = 2;
	int ret;

	if (addr & 0x80)
		len = 3;

	sb.i.len = len;
	sb.i.buf = b;

	b[0] = op | (addr & ENC28_MASK_ADDR);
	if ( (ret = spi_xfer(dev->spi_dev, SPI_F_DEFAULT, &sb.i, &sb.o)) )
		return ret;
	return b[len-1];
}

/* read and write do set the bank, so declare a set bank function first */
static void __enc28_set_bank(struct enc28_dev *dev, int addr)
{
	if (dev->bank == (addr & ENC28_MASK_BANK))
		return;

	dev->bank = addr & ENC28_MASK_BANK;
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_CLR, ENC28_CON1,
		      ENC28_CON1_BSEL1 | ENC28_CON1_BSEL0);
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_SET, ENC28_CON1,
		      dev->bank >> 5);
}

static int enc28_rd(struct enc28_dev *dev, int addr);
static int enc28_wr(struct enc28_dev *dev, int addr, int datum)
{
	int ret, check;
	__enc28_set_bank(dev, addr);
	ret = __enc28_wr_op(dev, ENC28_CMD_WRITE_CTRL_REG, addr, datum);
	if (DEBUG_ENC28) {
		check = enc28_rd(dev, addr);
		if (check != (datum & 0xff))
			printf("\tERROR: read %02x, written %02x\n",
			       check, datum & 0xff);
		else
			printf("\tOK: read %02x, written %02x\n",
			       check, datum & 0xff);
	}
	return ret;
}

static int enc28_rd(struct enc28_dev *dev, int addr)
{
	__enc28_set_bank(dev, addr);
	return __enc28_rd_op(dev, ENC28_CMD_READ_CTRL_REG, addr);
}

/* write a PHY register: data is 16 bits */
static void __enc28_wr_phy(struct enc28_dev *dev, int addr, int datum)
{
	enc28_wr(dev, ENC28_MIREGADR, addr);
	enc28_wr(dev, ENC28_MIWRL, datum);
	enc28_wr(dev, ENC28_MIWRH, datum >> 8);

	/* wait checking for status */
	do {
		udelay(15);
	} while (enc28_rd(dev, ENC28_MISTAT) & ENC28_MISTAT_BUSY);
}

/* write and read buffer memory -- do two spi transaction as we miss writev */
static int __enc28_wr_buf(struct enc28_dev *dev, const u8 *buf, int len)
{
	u8 cmd = ENC28_CMD_WRITE_BUF_MEM;
	int ret;

	ret = spi_raw_write(dev->spi_dev, SPI_F_NOFINI, 1, &cmd);
	if (!ret)
		ret = spi_raw_write(dev->spi_dev, SPI_F_NOINIT, len, buf);
	if (!ret)
		return len;

	return -1;
}

static int __enc28_rd_buf(struct enc28_dev *dev, u8 *buf, int len)
{
	u8 cmd = ENC28_CMD_READ_BUF_MEM;
	int ret;

	ret = spi_raw_write(dev->spi_dev, SPI_F_NOFINI, 1, &cmd);
	if (!ret)
		ret = spi_raw_read(dev->spi_dev, SPI_F_NOINIT, len, buf);
	if (!ret)
		return len;
	return -1;
}

int enc28_get_rev(struct enc28_dev *dev)
{
	return enc28_rd(dev, ENC28_REVID);
}

/* internal function for device init, spi has already been inited */
static int enc28_init(struct enc28_dev *dev)
{
	__enc28_wr_op(dev, ENC28_CMD_SOFT_RESET, 0, ENC28_CMD_SOFT_RESET);
	/* We shoulk check CLKRDY, but there's an errata, su just wait */
	__enc28_wr_op(dev, ENC28_CMD_SOFT_RESET, 0, ENC28_CMD_SOFT_RESET);
	__enc28_wr_op(dev, ENC28_CMD_SOFT_RESET, 0, ENC28_CMD_SOFT_RESET);
	__enc28_wr_op(dev, ENC28_CMD_SOFT_RESET, 0, ENC28_CMD_SOFT_RESET);
	__enc28_wr_op(dev, ENC28_CMD_SOFT_RESET, 0, ENC28_CMD_SOFT_RESET);
	mdelay(50);
	dev->bank = 0;

	/* Read all of them */
	if (DEBUG_ENC28) {
		int b, i, reg, val;
		for (b = 0; b < 4; b++) {
			for (i = 0; i < 0x20; i++) {
				reg = (b << 5) | i;
				val = enc28_rd(dev, reg);
				printf("RESET: 0x%02x = 0x%02x\n", reg, val);
				if (reg == 0 && val != 0xfa)
					return -1; /* EIO */
				if (reg == 1 && val != 0x05)
					return -1; /* EIO */
			}
		}
	}

	/*
	 * We receive from address 0 and send from 0x1a00. This allows
	 * one full TX frame up to end of memory (0x1fff).
	 */
	dev->rxptr = 0x0000;
	dev->txptr = 0x1a00;

	/* Set up internal packet pointers, low byte first. */

	enc28_wr(dev, ENC28_RXSTL, dev->rxptr); /* rxstart */
	enc28_wr(dev, ENC28_RXSTH, dev->rxptr >> 8);

	enc28_wr(dev, ENC28_RXRDPTL, dev->rxptr); /* rx read pointer */
	enc28_wr(dev, ENC28_RXRDPTH, dev->rxptr >> 8);

	enc28_wr(dev, ENC28_RXNDL, dev->txptr - 1); /* rx end */
	enc28_wr(dev, ENC28_RXNDH, (dev->txptr - 1) >> 8);

	enc28_wr(dev, ENC28_TXSTL, dev->txptr); /* tx start */
	enc28_wr(dev, ENC28_TXSTH, dev->txptr >> 8);

	enc28_wr(dev, ENC28_TXNDL, 0x1fff); /* tx end */
	enc28_wr(dev, ENC28_TXNDH, 0x1fff >> 8);

	/* Bank 1 stuff, packet filter:
	 * Accept all multicast and all broadcast.
	 * No other hardware filtering
	 */
	enc28_wr(dev, ENC28_RXFCON,
		 ENC28_RXFCON_UCEN |
		 //ENC28_RXFCON_CRCEN |
		 ENC28_RXFCON_MCEN |
		 ENC28_RXFCON_BCEN);

	/*
	 * Bank 2 stuff:
	 */

	/* enable MAC receive */
	enc28_wr(dev, ENC28_MACON1,
		 ENC28_MACON1_MARXEN |
		 ENC28_MACON1_TXPAUS |
		 ENC28_MACON1_RXPAUS);
	/* bring MAC out of reset */
	enc28_wr(dev, ENC28_MACON2, 0x00);
	/* enable automatic padding to 60bytes and CRC operations */
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_SET, ENC28_MACON3,
		      ENC28_MACON3_PADCFG0 |
		      ENC28_MACON3_TXCRCEN |
		      ENC28_MACON3_FRMLNEN);
	/* set inter-frame gap (non-back-to-back) */
	enc28_wr(dev, ENC28_MAIPGL, 0x12);
	enc28_wr(dev, ENC28_MAIPGH, 0x0C);
	/* set inter-frame gap (back-to-back) */
	enc28_wr(dev, ENC28_MABBIPG, 0x12);
	/* Set the maximum packet size which the controller will accept */
	/* and do not send packets longer than 1518: */
	enc28_wr(dev, ENC28_MAMXFLL, 1518);
	enc28_wr(dev, ENC28_MAMXFLH, 1518 >> 8);

	/*
	 * Do bank 3 stuff
	 */

	/* NOTE: MAC address in ENC28J60 is byte-backward */
	enc28_wr(dev, ENC28_MAADR5, dev->cfg->macaddr[0]);
	enc28_wr(dev, ENC28_MAADR4, dev->cfg->macaddr[1]);
	enc28_wr(dev, ENC28_MAADR3, dev->cfg->macaddr[2]);
	enc28_wr(dev, ENC28_MAADR2, dev->cfg->macaddr[3]);
	enc28_wr(dev, ENC28_MAADR1, dev->cfg->macaddr[4]);
	enc28_wr(dev, ENC28_MAADR0, dev->cfg->macaddr[5]);
	/* no loopback of transmitted frames */
	__enc28_wr_phy(dev, ENC28_PHCON2, ENC28_PHCON2_HDLDIS);

	/* switch back to bank 0 */
	__enc28_set_bank(dev, ENC28_CON1);
	/* enable interrutps */
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_SET, ENC28_IE,
		      ENC28_IE_INTIE |
		      ENC28_IE_PKTIE);
	/* enable packet reception */
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_SET, ENC28_CON1,
		      ENC28_CON1_RXEN);

	if (DEBUG_ENC28) {	/* Check contents of bank0 */
		int i, j;

		for (i = 0; i < 0x18; i++) {
			j = enc28_rd(dev, i);
			printf("check: %02x = %02x\n", i, j);
		}
		i = enc28_rd(dev, ENC28_MAADR5);
		if (i != dev->cfg->macaddr[0])
			printf("ERROR wrote %02x read %02x\n",
			       dev->cfg->macaddr[0], i);
		i = enc28_rd(dev, ENC28_MAADR4);
		if (i != dev->cfg->macaddr[1])
			printf("ERROR wrote %02x read %02x\n",
			       dev->cfg->macaddr[1], i);
	}

	return 0;
}

/* Public create and destroy functions */
struct enc28_dev *enc28_create(struct enc28_dev *dev)
{
	struct spi_dev *sdev;

	if (!dev) return dev;
	sdev = spi_create(dev->spi_dev);
	if (!sdev)
		return NULL;
	/* dev->cfg already set */
	if (enc28_init(dev) < 0)
		goto out_destroy;
	return dev;

out_destroy:
	spi_destroy(sdev);
	return NULL;
}

void enc28_destroy(struct enc28_dev *dev)
{
	spi_destroy(dev->spi_dev);
}

/* Public packet send and receive functions */
int enc28_send(struct enc28_dev *dev, const u8 *packet, int len)
{
	/* Set the write pointer and TXend pointer */
	enc28_wr(dev, ENC28_WRPTL, dev->txptr);
	enc28_wr(dev, ENC28_WRPTH, dev->txptr >> 8);
	enc28_wr(dev, ENC28_TXNDL, dev->txptr + len);
	enc28_wr(dev, ENC28_TXNDH, (dev->txptr + len) >> 8);

	/* Write per-packet control byte (0x00 means use macon3 settings) */
	__enc28_wr_op(dev, ENC28_CMD_WRITE_BUF_MEM, 0, 0x00);

	/* copy the packet into the transmit buffer and give tx cmd */
	__enc28_wr_buf(dev, packet, len);
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_SET, ENC28_CON1,
		      ENC28_CON1_TXRTS);

	/* Reset the transmit logic problem. Rev. B4 Silicon Errata point 12. */
	if( enc28_rd(dev, ENC28_IR) & ENC28_IR_TXERIF) {
		__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_CLR, ENC28_CON1,
			      ENC28_CON1_TXRTS);
	}
	return len;
}

int enc28_recv(struct enc28_dev *dev, u8 *packet, int maxlen)
{
	int len, next;
	u8 rxstat[6] __attribute__((aligned(2)));

	/* NOTE: checking IR_PKTIF doesn't work (B4 errata, point 6) */
	if (enc28_rd(dev, ENC28_PKTCNT) == 0)
		return -1; /* EAGAIN */

	/* Set the read pointer to the start of the received packet */
	enc28_wr(dev, ENC28_RDPTL, dev->rxptr);
	enc28_wr(dev, ENC28_RDPTH, dev->rxptr >> 8);

	/* Read in the preamble: next packet pointer and status vector */
	__enc28_rd_buf(dev, rxstat, sizeof(rxstat));

	next = rxstat[0] | (((u32)rxstat[1])<<16);
	len = rxstat[2] | (((u32)rxstat[3])<<16);
	len -= 4; /* ignore crc */
	if (len > maxlen)
		len = maxlen;
	dev->rxptr = next;

	/* Minimal error check: bit 23 says the CRC is ok with no symbol err */
	if ( !(rxstat[4] & 0x80) )
		return -1; /* EINVAL */

	__enc28_rd_buf(dev, packet, len);

	/* Move the RX read pointer to the start of the next received packet */
	enc28_wr(dev, ENC28_RXRDPTL, next);
	enc28_wr(dev, ENC28_RXRDPTH, next >> 8);
	/* Decrement packet counter, to tell the enc28 we are done */
	__enc28_wr_op(dev, ENC28_CMD_BIT_FIELD_SET, ENC28_CON2,
		      ENC28_CON2_PKTDEC);
	return len;
}

