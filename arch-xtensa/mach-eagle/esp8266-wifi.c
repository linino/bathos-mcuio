/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Wifi management */
#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/init.h>
#include <bathos/event.h>
#include <bathos/errno.h>
#include <bathos/dev_ops.h>
#include <bathos/allocator.h>
#include <bathos/buffer_queue_server.h>
#include <ets_sys.h>
#include <osapi.h>
#include <user_interface.h>
#include <netif/wlan_lwip_if.h>
#include <bathos/wifi.h>
#include <bathos/netif.h>
#include <mach/esp8266-wifi.h>

static struct wifi_connected_event_data ced;
static struct wifi_disconnected_event_data ded;

declare_event(netif_up);
declare_event(netif_down);
declare_event(wifi_connected);
declare_event(wifi_disconnected);

static void wifi_callback(System_Event_t *evt)
{
	switch (evt->event)
	{
	case EVENT_STAMODE_CONNECTED:
	{
		printf("connect to ssid %s, channel %d\n",
		       evt->event_info.connected.ssid,
		       evt->event_info.connected.channel);
		strncpy(ced.ssid, evt->event_info.connected.ssid,
			MAX_SSID_LENGTH);
		ced.channel = evt->event_info.connected.channel;
		trigger_event(&event_name(wifi_connected), &ced);
		break;
	}

	case EVENT_STAMODE_DISCONNECTED:
	{
		printf("disconnect from ssid %s, reason %d\n",
		       evt->event_info.disconnected.ssid,
		       evt->event_info.disconnected.reason);
		strncpy(ded.ssid, evt->event_info.connected.ssid,
			MAX_SSID_LENGTH);
		ded.reason = evt->event_info.disconnected.reason;
		trigger_event(&event_name(wifi_disconnected), &ded);
		trigger_event(&event_name(netif_down), NULL);
		break;
	}

	case EVENT_STAMODE_GOT_IP:
	{
		printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
		       IP2STR(&evt->event_info.got_ip.ip),
		       IP2STR(&evt->event_info.got_ip.mask),
		       IP2STR(&evt->event_info.got_ip.gw));
		printf("\n");
		trigger_event(&event_name(netif_up), evt);
		break;
	}
	default:
	{
		break;
	}
	}
}

static int wifi_init(void)
{
	static struct station_config config;

	/* Init wifi */
	printf("esp8266 wifi init\n");
	wifi_station_set_hostname(ESP8266_STATION_HOSTNAME);
	wifi_set_opmode_current(STATION_MODE);
	config.bssid_set = 0;
	memcpy(&config.ssid, ESP8266_ESSID, 32);
	memcpy(&config.password, ESP8266_PASSWD, 64);
	wifi_station_set_config(&config);
	wifi_set_event_handler_cb(wifi_callback);
	return 0;
}
core_initcall(wifi_init);
