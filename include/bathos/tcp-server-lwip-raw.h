#ifndef __TCP_SERVER_LWIP_RAW_H__
#define __TCP_SERVER_LWIP_RAW_H__

#include <bathos/tcp-lwip-raw.h>

#ifdef LWIP_TCP

static inline int tcp_server_socket_lwip_raw_init(struct tcp_socket_lwip_raw *r,
						  unsigned short port)
{
	return tcp_socket_lwip_raw_init(r, port, 1);
}

static inline int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *cd,
						  const void *buf,
						  unsigned int len)
{
	return tcp_socket_lwip_raw_send(cd, buf, len);
}

static inline int tcp_server_socket_lwip_raw_close(struct tcp_conn_data *cd)
{
	return tcp_socket_lwip_raw_close(cd);
}

static inline int tcp_server_socket_lwip_raw_abort(struct tcp_conn_data *cd)
{
	return tcp_socket_lwip_raw_abort(cd);
}

static inline int tcp_server_socket_lwip_raw_fini(struct tcp_socket_lwip_raw *r)
{
	return tcp_socket_lwip_raw_fini(r);
}

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
