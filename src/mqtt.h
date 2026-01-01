#ifndef MQTT_H
#define MQTT_H

#include <MQTTClient.h>

#define MQTT_PUB_CLIENT_ID "mqtt_publisher"
#define MQTT_SUB_CLIENT_ID "mqtt_subscriber"
#define MQTT_PUB_TOPIC "sensor/data"
#define MQTT_SUB_TOPIC "control/command"

extern MQTTClient mqtt_pub_client;
extern MQTTClient mqtt_sub_client;

void* mqtt_publisher_thread(void *arg);
void* mqtt_subscriber_thread(void *arg);

#endif