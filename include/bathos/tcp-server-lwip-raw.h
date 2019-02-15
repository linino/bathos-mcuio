#ifndef __TCP_SERVER_LWIP_RAW_H__
#define __TCP_SERVER_LWIP_RAW_H__

#ifdef CONFIG_HAVE_LWIP

#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"

#endif /* CONFIG_HAVE_LWIP */

#ifdef LWIP_TCP

/* Forward decl */
struct tcp_server_socket_lwip_raw;

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
	struct tcp_server_socket_lwip_raw *raw_socket;
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


struct tcp_server_socket_lwip_raw_ops {
	/* Returns 0 if OK, -1 if ERROR */
	int (*accept)(struct tcp_conn_data *);
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
};

struct tcp_server_socket_lwip_raw {
	const struct tcp_server_socket_lwip_raw_ops *ops;
	void (*netif_idle)(struct netif *);
	struct netif *netif;
	struct tcp_pcb *tcp_conn_pcb;
	struct bathos_pipe *pipe;
	void *server_priv;
};

int tcp_server_socket_lwip_raw_init(struct tcp_server_socket_lwip_raw *,
				    unsigned short port);

int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *,
				    const void *buf, unsigned int len);

int tcp_server_socket_lwip_raw_close(struct tcp_conn_data *);

int tcp_server_socket_lwip_raw_abort(struct tcp_conn_data *);

int tcp_server_socket_lwip_raw_fini(struct tcp_server_socket_lwip_raw *);

#else /* !LWIP_TCP */

struct tcp_server_socket_lwip_raw;
struct tcp_conn_data;

static inline int
tcp_server_socket_lwip_raw_init(struct tcp_server_socket_lwip_raw *r,
				unsigned short port)
{
	return -1;
}

static inline int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *d,
						  const void *buf,
						  unsigned int len)
{
	return -1;
}

static inline int tcp_server_socket_lwip_raw_close(struct tcp_conn_data *d)
{
	return -1;
}

static inline int tcp_server_socket_lwip_raw_abort(struct tcp_conn_data *d)
{
	return -1;
}

static inline int
tcp_server_socket_lwip_raw_fini(struct tcp_server_socket_lwip_raw *r)
{
	return -1;
}

#endif /* LWIP_TCP */

#endif /* __TCP_SERVER_LWIP_RAW_H__ */