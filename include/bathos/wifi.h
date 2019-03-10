/*
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
#include <bathos/bathos.h>
#include <bathos/event.h>

#define MAX_SSID_LENGTH 32

struct wifi_connected_event_data {
	char ssid[MAX_SSID_LENGTH + 1];
	int channel;
};

declare_extern_event(wifi_connected);

struct wifi_disconnected_event_data {
	char ssid[MAX_SSID_LENGTH + 1];
	int reason;
};

declare_extern_event(wifi_disconnected);
