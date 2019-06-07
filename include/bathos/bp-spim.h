/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */

#ifndef __BP_SPIM_H__
#define __BP_SPIM_H__

#include <stdint.h>
#include <bathos/dev_ops.h>

struct bp_spim_priv;

struct bp_spim_platform_data {
	/* 0, 1 or 2 */
	int instance;
	int nbufs;
	int bufsize;
	int cs_active_state;
};

extern const struct bathos_dev_ops PROGMEM bp_spim_dev_ops;

#endif /* __BP_SPIM_H__ */
