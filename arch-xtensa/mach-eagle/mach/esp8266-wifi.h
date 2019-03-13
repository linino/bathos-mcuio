/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Wifi management */
#ifndef __ESP8266_WIFI_H__
#define __ESP8266_WIFI_H__

#include <generated/autoconf.h>
#include <bathos/bathos.h>
#include <bathos/event.h>

#ifndef ESP8266_ESSID
#define ESP8266_ESSID CONFIG_DEFAULT_ESP8266_ESSID
#endif
#ifndef ESP8266_PASSWD
#define ESP8266_PASSWD CONFIG_DEFAULT_ESP8266_PASSWD
#endif
#ifndef ESP8266_STATION_HOSTNAME
#define ESP8266_STATION_HOSTNAME CONFIG_DEFAULT_ESP8266_STATION_HOSTNAME
#endif

declare_extern_event(esp8266_wifi_connected);
declare_extern_event(esp8266_wifi_disconnected);
declare_extern_event(esp8266_wifi_got_ip);

#endif /* __ESP8266_WIFI_H__ */
