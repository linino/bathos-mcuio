/*
 * Copyright (c) dog hunter AG - Zug - CH
 * General Public License version 2 (GPLv2)
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 */
/* Wifi management */
#ifndef __ESP8266_WIFI_H__
#define __ESP8266_WIFI_H__

#include <bathos/bathos.h>
#include <bathos/event.h>

declare_extern_event(esp8266_wifi_connected);
declare_extern_event(esp8266_wifi_disconnected);
declare_extern_event(esp8266_wifi_got_ip);

#endif /* __ESP8266_WIFI_H__ */
