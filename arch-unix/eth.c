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
#include <bathos/unix-eth.h>

struct unix_eth_platform_data eth0_pdata = {
	/* FIXME: make platform data dynamic */
	.ifname = "eth0",
	.eth_type = 0x86b5,
	.bufsize = 2048,
	.nbufs = 8,
};

static struct bathos_dev __eth0
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "eth0",
	.ops = &unix_eth_dev_ops,
	.platform_data = &eth0_pdata,
};

