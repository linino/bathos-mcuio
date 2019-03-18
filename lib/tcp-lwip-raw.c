/*
 * Raw tcp/ip server using lwip
 * This code comes from libdfu with minor modifications
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2017 2019
 */
#include <bathos/bathos.h>
#include <bathos/stdio.h>
#include <generated/autoconf.h>
#include <bathos/tcp-lwip-raw.h>


#ifdef LWIP_TCP

#ifndef CONFIG_MAX_TCP_SERVER_CONNECTIONS
#define MAX_CONNECTIONS 4
#else
#define MAX_CONNECTIONS CONFIG_MAX_TCP_SERVER_CONNECTIONS
#endif

static struct tcp_conn_data connections[MAX_CONNECTIONS];

static struct tcp_conn_data *alloc_connection(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(connections); i++)
		if (connections[i].state == ES_FREE) {
			connections[i].state = ES_NONE;
			connections[i].must_close = 0;
			pr_debug("%s: connection %p allocated\n", __func__,
				 &connections[i]);
			return &connections[i];
		}
	return NULL;
}

static void free_connection(struct tcp_conn_data *c)
{
	pr_debug("%s: freeing connection %p\n", __func__, c);
	c->state = ES_FREE;
}

static void tcp_conn_free(struct tcp_conn_data *es)
{
	free_connection(es);
}

static void tcp_conn_close(struct tcp_pcb *tpcb, struct tcp_conn_data *es)
{
	struct tcp_socket_lwip_raw *r = es->raw_socket;

	if (tcp_close(tpcb) != ERR_OK) {
		/*
		 * http://lwip.wikia.com/wiki/Raw/TCP, tcp_close:
		 * The function may
		 * return ERR_MEM if no memory was available for closing the
		 * connection. If so, the application should wait and try
		 * again either by using the acknowledgment callback or the
		 * polling functionality. If the close succeeds, the function
		 * returns ERR_OK.
		 */
		es->must_close = 1;
		pr_debug("%s: must_close = 1\n", __func__);
		return;
	}
	pr_debug("%s: must_close = 0\n", __func__);
	es->must_close = 0;
	tcp_arg(tpcb, NULL);
	tcp_sent(tpcb, NULL);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, NULL);
	tcp_poll(tpcb, NULL, 0);
	if (r->ops->closed)
		r->ops->closed(es);
	tcp_conn_free(es);
}

err_t tcp_conn_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct tcp_conn_data *es;
	struct tcp_socket_lwip_raw *r;
	struct pbuf *ptr;
	int stat = 0;

	LWIP_ASSERT("arg != NULL", arg);
	es = (struct tcp_conn_data *)arg;
	r = es->raw_socket;
	LWIP_ASSERT("raw_socket != NULL", r);

	if (err != ERR_OK) {
		if (p)
			/* cleanup, for unkown reason */
			pbuf_free(p);
		return err;
	}

	if (!p) {
		pr_debug("%s: remote end closed\n", __func__);
		/* remote host closed connection */
		es->state = ES_CLOSING;
		if (r->ops->poll)
			stat = r->ops->poll(es);
		if(!stat)
			/* we're done sending, close it */
			tcp_conn_close(tpcb, es);
		return ERR_OK;
	}

	for (ptr = p; ; ptr = ptr->next) {
		tcp_recved(es->pcb, ptr->len);
		if (r->ops->recv) {
			stat = r->ops->recv(es, ptr->payload, ptr->len);
			if (stat < 0)
				break;
		}
		if (ptr->tot_len == ptr->len)
			break;
	}
	pbuf_free(p);
	return ERR_OK;
}

void tcp_active_conn_error(void *arg, err_t err)
{
	struct tcp_conn_data *es;
	struct tcp_socket_lwip_raw *r;

	es = arg;
	if (!es)
		return;
	r = es->raw_socket;
	r->active_conn_error = 1;
	if (r->ops->error)
		r->ops->error(es, err);

	tcp_conn_free(es);
}

void tcp_conn_error(void *arg, err_t err)
{
	struct tcp_conn_data *es;
	struct tcp_socket_lwip_raw *r;

	es = (struct tcp_conn_data *)arg;
	pr_debug("%s\n", __func__);

	if (!es || !es->raw_socket)
		return;
	r = es->raw_socket;

	if (r->ops->error)
		r->ops->error(es, err);

	tcp_conn_free(es);
}

static err_t tcp_conn_poll(void *arg, struct tcp_pcb *tpcb)
{
	struct tcp_conn_data *es;
	struct tcp_socket_lwip_raw *r;

	es = (struct tcp_conn_data *)arg;
	if (!es || !es->raw_socket) {
		/* nothing to be done */
		tcp_abort(tpcb);
		return ERR_ABRT;
	}

	r = es->raw_socket;
	if (es->must_close || !r->ops->poll || !r->ops->poll(es)) {
		pr_debug("%s: closing\n", __func__);
		tcp_conn_close(tpcb, es);
	}

	return ERR_OK;
}

static err_t tcp_conn_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	struct tcp_conn_data *es;
	struct tcp_socket_lwip_raw *r;

	LWIP_UNUSED_ARG(len);

	es = arg;
	es->acknowledged += len;
	pr_debug("%s: written = %u, acknowledged = %u\n", __func__,
		 es->written, es->acknowledged);
	r = es->raw_socket;
	if (r->ops->sent)
		r->ops->sent(es);
	return ERR_OK;
}

static err_t tcp_conn_established(void *arg, struct tcp_pcb *newpcb, err_t err,
				  int server)
{
	struct tcp_conn_data *es;
	struct tcp_socket_lwip_raw *r;
	int stat = 0;

	if (server)
		r = arg;
	else {
		es = arg;
		r = es->raw_socket;
	}

	pr_debug("%s %d, err = %d, newpcb = %p, r = %p\n", __func__, __LINE__,
		 err, newpcb, r);
	if ((err != ERR_OK) || (newpcb == NULL) || !r)
		return ERR_VAL;
	/*
	 * Unless this pcb should have NORMAL priority, set its priority now.
	 * When running out of pcbs, low priority pcbs can be aborted to create
	 * new pcbs of higher priority.
	 */
	tcp_setprio(newpcb, TCP_PRIO_MIN);

	if (server) {
		es = alloc_connection();
		if (!es)
			return ERR_MEM;
		es->state = ES_CONNECTED;
		es->pcb = newpcb;
		es->raw_socket = r;
		es->acknowledged = es->written = 0;
		if (r->ops->accept)
			stat = r->ops->accept(es);
		if (stat) {
			free_connection(es);
			return ERR_MEM;
		}
	} else {
		if (r->ops->connected) {
			stat = r->ops->connected(es);
			if (stat) {
				free_connection(es);
				return ERR_MEM;
			}
		}
	}
	/* pass newly allocated es to our callbacks */
	tcp_arg(newpcb, es);
	tcp_recv(newpcb, tcp_conn_recv);
	tcp_err(newpcb, tcp_conn_error);
	tcp_poll(newpcb, tcp_conn_poll, 0);
	tcp_sent(newpcb, tcp_conn_sent);
	tcp_nagle_disable(newpcb);
	return ERR_OK;
}

static err_t tcp_conn_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	return tcp_conn_established(arg, newpcb, err, 1);
}

static err_t tcp_socket_lwip_connected(void * arg, struct tcp_pcb *tpcb,
				       err_t err)
{
	return tcp_conn_established(arg, tpcb, err, 0);
}

int tcp_socket_lwip_raw_init(struct tcp_socket_lwip_raw *r,
			     struct ip_addr * ipaddr,
			     unsigned short port, int server)
{
	err_t err;

	if (!r->ops) {
		printf("%s: ops is NULL\n", __func__);
		return -1;
	}
	r->tcp_conn_pcb = tcp_new();
	if (!r->tcp_conn_pcb) {
		printf("%s: tcp_new() returns error\n", __func__);
		return -1;
	}
	if (server) {
		/* server */
		pr_debug("%s: binding to port %u\n", __func__, port);
		tcp_arg(r->tcp_conn_pcb, r);
		err = tcp_bind(r->tcp_conn_pcb, ipaddr, port);
		if (err != ERR_OK) {
			printf("%s: tcp_bind() to port %u returns error %d\n",
			       __func__, port, err);
			return -1;
		}
		r->tcp_conn_pcb = tcp_listen(r->tcp_conn_pcb);
		if (!r->tcp_conn_pcb) {
			printf("%s: tcp_listen() returns NULL\n", __func__);
			return -1;
		}
		tcp_accept(r->tcp_conn_pcb, tcp_conn_accept);
	} else {
		struct tcp_conn_data *es;

		/* client */
		pr_debug("%s: connecting to %s\n", __func__,
			 ipaddr_ntoa(ipaddr));
		es = alloc_connection();
		if (!es) {
			printf("%s: cannot allocate connection\n", __func__);
			return ERR_MEM;
		}
		es->state = ES_CONNECTED;
		es->pcb = r->tcp_conn_pcb;
		es->raw_socket = r;
		es->acknowledged = es->written = 0;
		tcp_arg(r->tcp_conn_pcb, es);
		tcp_err(r->tcp_conn_pcb, tcp_active_conn_error);
		r->active_conn_error = 0;
		err = tcp_connect(r->tcp_conn_pcb, ipaddr, port,
				  tcp_socket_lwip_connected);
		if (err != ERR_OK) {
			printf("%s: tcp_connect() returns error\n", __func__);
			free_connection(es);
			return -1;
		}
	}
	return 0;
}

int tcp_socket_lwip_raw_fini(struct tcp_socket_lwip_raw *r)
{
	if (!r->tcp_conn_pcb) {
		printf("%s: no socket\n", __func__);
		return -1;
	}
	tcp_arg(r->tcp_conn_pcb, NULL);
	if (!r->active_conn_error)
		tcp_abort(r->tcp_conn_pcb);
	return 0;
}

extern void my_lwip_idle(void);

int tcp_socket_lwip_raw_send(struct tcp_conn_data *es,
			     const void *buf, unsigned int len)
{
	err_t stat;
	uint16_t l;
	const char *ptr = buf;

	es->written = 0;
	do {

		l = min(len - es->written, tcp_sndbuf(es->pcb));
		if (!l) {
			pr_debug("%s: no more space in output buffer\n",
				 __func__);
			return es->written;
		}
		pr_debug("%s: tcp_write(%p, len = %u, written = %d, l = %d)\n",
			 __func__, &ptr[es->written], len, es->written, l);
		stat = tcp_write(es->pcb, &ptr[es->written], l,
				 TCP_WRITE_FLAG_COPY);
		if (stat != ERR_OK) {
			printf("%s: tcp_write returned error %d\n", __func__,
			       stat);
			es->raw_socket->active_conn_error = 1;
			break;
		}
		es->written += l;
		pr_debug("%s: written = %d\n", __func__, es->written);
		if (es->written >= len)
			break;
		stat = tcp_output(es->pcb);
	} while(stat == ERR_OK);

	return stat == ERR_OK ? es->written : -1;
}

int tcp_socket_lwip_raw_close(struct tcp_conn_data *es)
{
	struct tcp_socket_lwip_raw *r = es->raw_socket;

	pr_debug("%s\n", __func__);
	if (!r->active_conn_error)
		tcp_conn_close(es->pcb, es);
	return 0;
}

/* Like close, but should be synchronous */
int tcp_socket_lwip_raw_abort(struct tcp_conn_data *es)
{
	struct tcp_socket_lwip_raw *r = es->raw_socket;
	struct tcp_pcb *tpcb = es->pcb;

	tcp_abort(tpcb);
	es->must_close = 0;
	tcp_arg(tpcb, NULL);
	tcp_sent(tpcb, NULL);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, NULL);
	tcp_poll(tpcb, NULL, 0);
	if (r->ops->closed)
		r->ops->closed(es);
	tcp_conn_free(es);
	return 0;
}

#endif /* LWIP_TCP */
