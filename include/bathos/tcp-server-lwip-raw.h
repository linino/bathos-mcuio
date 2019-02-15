#ifndef __TCP_SERVER_LWIP_RAW_H__
#define __TCP_SERVER_LWIP_RAW_H__

#include <bathos/tcp-lwip-raw.h>

#ifdef LWIP_TCP

int tcp_server_socket_lwip_raw_init(struct tcp_socket_lwip_raw *,
				    unsigned short port);

int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *,
				    const void *buf, unsigned int len);

int tcp_server_socket_lwip_raw_close(struct tcp_conn_data *);

int tcp_server_socket_lwip_raw_abort(struct tcp_conn_data *);

int tcp_server_socket_lwip_raw_fini(struct tcp_socket_lwip_raw *);

#else /* !LWIP_TCP */

struct tcp_socket_lwip_raw;
struct tcp_conn_data;

static inline int
tcp_server_socket_lwip_raw_init(struct tcp_socket_lwip_raw *r,
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
tcp_server_socket_lwip_raw_fini(struct tcp_socket_lwip_raw *r)
{
	return -1;
}

#endif /* LWIP_TCP */

#endif /* __TCP_SERVER_LWIP_RAW_H__ */
