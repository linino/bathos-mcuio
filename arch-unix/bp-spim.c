/*
 * Copyright (c) Davide Ciminaghi 2017
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/buffer_queue_server.h>
#include <bathos/bp-spim.h>

#ifdef CONFIG_BP_SPIM_DRIVER

static struct bp_spim_platform_data bp_spim0_pdata = {
	.instance = 0,
	.nbufs = 80,
	.bufsize = 32,
	.cs_active_state = 0,
};

static struct bathos_dev __eth0
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "spim0",
	.ops = &bp_spim_dev_ops,
	.platform_data = &bp_spim0_pdata,
};

#endif

