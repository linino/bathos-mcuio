#ifndef __FREESCALE_UART_H__
#define __FREESCALE_UART_H__

#include <stdint.h>
#include <bathos/dev_ops.h>

struct freescale_uart_priv;

struct freescale_uart_platform_data {
	int index;
	uint8_t tx_pin;
	uint8_t rx_pin;
	int irq;
	int afnum;
	uint32_t base;
	/* Support for freescale uart variants ? Unused at present */
	int flags;
	int (*power)(struct freescale_uart_priv *, int on);
};

extern const struct bathos_dev_ops PROGMEM freescale_uart_dev_ops;
extern void freescale_uart_irq_handler(struct bathos_dev *);


#endif /* __FREESCALE_UART_H__ */
