#ifndef __TCP_LWIP_RAW_H__
#define __TCP_LWIP_RAW_H__

#ifdef CONFIG_HAVE_LWIP

#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"

#endif /* CONFIG_HAVE_LWIP */

#ifdef LWIP_TCP

/* Forward decl */
struct tcp_socket_lwip_raw;

/* Connection state */
enum tcp_conn_state
{
	ES_FREE = 0,
	ES_NONE,
	ES_CONNECTED,
	ES_CLOSING
};

struct bathos_pipe;

/* struct representing a tcp connection */
struct tcp_conn_data
{
	enum tcp_conn_state state;
	struct tcp_socket_lwip_raw *raw_socket;
	struct tcp_pcb *pcb;
	uint16_t written;
	uint16_t acknowledged;
	int must_close;
	/* connection can be closed when this is !0 */
	int can_close;
	/* related pipe (if any) */
	struct bathos_pipe *pipe;
	/* Server private data belonging to this connection */
	void *priv;
};


struct tcp_socket_lwip_raw_ops {
	/* Returns 0 if OK, -1 if ERROR (server) */
	int (*accept)(struct tcp_conn_data *);
	/* Invoked on active connection completed */
	int (*connected)(struct tcp_conn_data *);
	/* Returns number of stored bytes or -1 if not enough memory */
	int (*recv)(struct tcp_conn_data *,
		    const void *buf,
		    unsigned int len);
	/* Invoked on bytes sent */
	int (*sent)(struct tcp_conn_data *);
	/*
	 * Returns 0 if we can close connection, !0 if we still have stuff
	 * to send
	 */
	int (*poll)(struct tcp_conn_data *);
	/* Invoked on actual connection close */
	void (*closed)(struct tcp_conn_data *);
	/* Invoked on connection error */
	void (*error)(struct tcp_conn_data *, int error);
};

struct tcp_socket_lwip_raw {
	const struct tcp_socket_lwip_raw_ops *ops;
	void (*netif_idle)(struct netif *);
	struct netif *netif;
	struct tcp_pcb *tcp_conn_pcb;
	struct bathos_pipe *pipe;
	void *server_priv;
};

extern int tcp_socket_lwip_raw_abort(struct tcp_conn_data *);
extern int tcp_socket_lwip_raw_close(struct tcp_conn_data *);
extern int tcp_socket_lwip_raw_fini(struct tcp_socket_lwip_raw *r);
extern int tcp_socket_lwip_raw_send(struct tcp_conn_data *es,
				    const void *buf, unsigned int len);
extern int tcp_socket_lwip_raw_init(struct tcp_socket_lwip_raw *r,
				    struct ip_addr * ipaddr,
				    unsigned short port, int server);

#endif /* LWIP_TCP */

#endif /* __TCP_SERVER_LWIP_RAW_H__ */