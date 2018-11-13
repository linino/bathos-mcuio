/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef __ESP8266_SPIM_H__
#define __ESP8266_SPIM_H__

#include <stdint.h>
#include <bathos/dev_ops.h>

struct esp8266_spim_priv;

struct esp8266_spim_platform_data {
	/* 0, 1 or 2 */
	int instance;
	int nbufs;
	int bufsize;
	uint32_t base;
	/* Receives instance index, sets up pins via PIN_FUNC_SELECT */
	int (*setup_pins)(int);
	/* Spi mode ? */
};

extern const struct bathos_dev_ops PROGMEM esp8266_spim_dev_ops;

extern void esp8266_spim_int_handler(struct bathos_dev *);

#endif /* __ESP8266_SPIM_H__ */
