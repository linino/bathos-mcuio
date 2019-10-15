
#include <user_interface.h>
#include <c_types.h>
#include <ets_sys.h>
#include <osapi.h>
#include <bathos/bathos.h>
#include <bathos/init.h>
#include <bathos/jiffies.h>
#include <bathos/event.h>
#include <bathos/tcp-server-connection-dev.h>
#include <bathos/tcp-client-pipe.h>


#define BATHOS_QUEUE_LEN 16

static os_event_t bathos_queue[BATHOS_QUEUE_LEN];

static void _bathos_main(os_event_t *events)
{
	unsigned long start = jiffies;

	while(pending_events() && !(time_after(jiffies, start + HZ/2))) {
		if (time_after(jiffies, start + HZ/3))
			pr_debug("!!! %lu %lu\n", start, jiffies);
		handle_events();
	}
}

/*
 * The bathos is just a task in the esp8266's os.
 * In this case, bathos loop is not actually a loop. We just spawn a task
 * here and wake it up for the first time. Then trigger_event() will restart
 * _bathos_main via events_notify();
 */
void bathos_loop(void)
{
	system_os_task(_bathos_main, 0, bathos_queue, BATHOS_QUEUE_LEN);
	system_os_post(0, 0, 0);
}

/*
 * This is called by trigger_event(): we need to wake up _bathos_main() when
 * bathos events are available to be processed
 */
void events_notify(void)
{
	system_os_post(0, 0, 0);
}

/* http://cholla.mmto.org/esp8266/xtensa.html */
/* FIXME: HAVE A LOOK HERE : */
/* https://backend.cesanta.com/blog/esp8266-gdb.shtml */
struct exception_frame
{
	uint32_t epc;
	uint32_t ps;
	uint32_t sar;
	uint32_t unused;
	union {
		struct {
			uint32_t a0;
			uint32_t a2;
			uint32_t a3;
			uint32_t a4;
			uint32_t a5;
			uint32_t a6;
			uint32_t a7;
			uint32_t a8;
			uint32_t a9;
			uint32_t a10;
			uint32_t a11;
			uint32_t a12;
			uint32_t a13;
			uint32_t a14;
			uint32_t a15;
		};
		uint32_t a_reg[15];
	};
	uint32_t cause;
};


extern void
_xtos_set_exception_handler(int,
			    void (*ptr)(struct exception_frame *ef,
					uint32_t cause));
extern int ets_printf(const char *fmt, ...);

static void my_eh(struct exception_frame *ef, uint32_t cause)
{
	uint32_t *p, *sp;

	ets_printf("# exception %u\n", cause);
	ets_printf("# pc = %08x\n", ef->epc);
	/* Call return address */
	ets_printf("# a0 = %08x\n", ef->a0);
	/* Stack pointer */
	sp = (uint32_t *)((unsigned int)ef - 256);
	ets_printf("# sp = %08x\n", sp);
	/* Print out some stack */
	for(p = sp; p < sp + 1024; p += 4) {
		if((unsigned int)p >= 0x3fffffff)
			break; /* End of ram */
		ets_printf("# %p: %08x %08x %08x %08x\n",
			   p, p[0], p[1], p[2], p[3]);
	}
	/* Panic */
	while(1);
}

void user_init(void)
{
	int i;
	static const int exceptions[] = {
		6, 9, 28, 29,
	};

	console_early_init();
	/* printf seems to start working after a while ! */
	for (i = 0; i < 11500; i++)
		console_putc('+');

	for (i = 0; i < ARRAY_SIZE(exceptions); i++)
		     _xtos_set_exception_handler(i, my_eh);
	bathos_setup();
	bathos_main();
}


#ifdef CONFIG_ENABLE_TCP_SERVER_DEV

static struct bathos_dev tcp_connection_dev
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "tcp-server-connection",
	.ops = &tcp_server_connection_dev_ops,
};

static struct bathos_dev tcp_client_dev
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "tcp-client-connection",
	.ops = &tcp_client_dev_ops,
};

extern const struct bathos_dev_ops tcp_server_dev_ops;

static struct bathos_dev tcp_dev
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "tcp-server-main",
	.ops = &tcp_server_dev_ops,
};

extern const struct bathos_dev_ops mqtt_client_dev_ops;

static struct bathos_dev mqtt_dev
__attribute__((section(".bathos_devices"), aligned(4), used)) = {
	.name = "mqtt-dev",
	.ops = &mqtt_client_dev_ops,
};

#endif /* CONFIG_ENABLE_TCP_SERVER_DEV */
