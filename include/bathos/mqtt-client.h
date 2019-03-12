/*
 * mqtt client functions, utilities and events
 *
 * Copyright (c) DogHunter LLC and the Linino organization 2019
 * Author Davide Ciminaghi 2019
 */
#include <bathos/bathos.h>
#include <bathos/event.h>
#include <bathos/mqtt_pipe.h>

struct mqtt_bathos_client;

struct mqtt_bathos_client *
bathos_mqtt_publisher_init(const struct mqtt_client_data *mcd,
			   const struct event * PROGMEM publisher_ready_event,
			   const struct event * PROGMEM publisher_error_event);


int bathos_mqtt_publish(struct mqtt_bathos_client *,
			char *topic, void *data, int data_len,
			int flags);

int bathos_mqtt_publisher_fini(const struct mqtt_bathos_client *);
