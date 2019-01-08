#include <arch/hw.h>
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/errno.h>
#include <tasks/mcuio.h>

#include "mcuio-function.h"


static const struct mcuio_func_descriptor PROGMEM zero_descr = {
	.device = 0xdead,
	.vendor = 0xbeef,
	.rev = 0,
	.class = 0,
};

const unsigned int PROGMEM zero_descr_length = sizeof(zero_descr);

const char PROGMEM zero_ro_contents[8] = "mcuio";

const unsigned int PROGMEM zero_ro_contents_length = sizeof(zero_ro_contents);

const char PROGMEM zero_rw_contents[8] = "deadbeef";

const unsigned int PROGMEM zero_rw_contents_length = sizeof(zero_rw_contents);

static const struct mcuio_range PROGMEM zero_ranges[] = {
	{
		.start = 0,
		.length = &zero_descr_length,
		.rd_target = &zero_descr,
		.ops = &default_mcuio_range_ro_ops,
	},
	{
		.start = 8,
		.length = &zero_ro_contents_length,
		.rd_target = (char *)zero_ro_contents,
		.ops = &default_mcuio_range_ro_ops,
	},
	{
		.start = 16,
		.length = &zero_rw_contents_length,
		.rd_target = zero_rw_contents,
		.wr_target = zero_rw_contents,
		.ops = &default_mcuio_range_ops,
	},
};


declare_mcuio_function(zero, zero_ranges, NULL, NULL,
		       &mcuio_func_common_runtime);
