/*
 * Unix low level eth driver, uses packet socket (see man 7 packet).
 * General Public License version 2 (GPLv2)
 * Author Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#ifndef __UNIX_ETH_H__
#define __UNIX_ETH_H__

struct unix_eth_platform_data {
	char *ifname;
	unsigned short eth_type;
	int nbufs;
	int bufsize;
};

extern const struct bathos_dev_ops unix_eth_dev_ops;

#endif /* __UNIX_ETH_H__ */


