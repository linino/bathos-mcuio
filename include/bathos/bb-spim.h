/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef __BB_SPIM_H__
#define __BB_SPIM_H__

#include <stdint.h>
#include <bathos/dev_ops.h>

struct bb_spim_priv;

struct bb_spim_platform_data {
	int instance;
	int nbufs;
	int bufsize;
	int miso_gpio;
	int mosi_gpio;
	int clk_gpio;
	int cs_gpio;
	/* In KHz */
	int clk_freq;
};

extern const struct bathos_dev_ops PROGMEM bb_spim_dev_ops;

#endif /* __ESP8266_SPIM_H__ */
