#include "mqtt.h"
#include "shared.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>

MQTTClient mqtt_pub_client;
MQTTClient mqtt_sub_client;

int mqtt_message_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    char payload[512];
    snprintf(payload, sizeof(payload), "%.*s", (int)message->payloadlen, (char*)message->payload);
    
    printf("[MQTT-SUB] Topic: %s, Payload: %s\n", topicName, payload);
    
    struct json_object *parsed = json_tokener_parse(payload);
    if (parsed) {
        struct json_object *pump_id_obj, *state_obj, *status_obj;
        
        // Nhận lệnh điều khiển từ HTTP API
        if (json_object_object_get_ex(parsed, "pump_id", &pump_id_obj) &&
            json_object_object_get_ex(parsed, "state", &state_obj)) {
            
            int pump_id = json_object_get_int(pump_id_obj);
            int state = json_object_get_int(state_obj);
            
            update_pump_status(pump_id, state);
        }
        
        // Nhận feedback từ hardware
        if (json_object_object_get_ex(parsed, "pump_id", &pump_id_obj) &&
            json_object_object_get_ex(parsed, "status", &status_obj)) {
            
            int pump_id = json_object_get_int(pump_id_obj);
            int status = json_object_get_int(status_obj);
            
            update_pump_feedback(pump_id, status);
        }
        
        json_object_put(parsed);
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void* mqtt_publisher_thread(void *arg) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message msg = MQTTClient_message_initializer;
    int rc;
    
    sleep(1);
    
    MQTTClient_create(&mqtt_pub_client, BROKER, "pump_mqtt_pub", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = USERNAME;
    conn_opts.password = PASSWORD;
    
    printf("[MQTT-PUB] Connecting...\n");
    if ((rc = MQTTClient_connect(mqtt_pub_client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[MQTT-PUB] Failed, rc=%d\n", rc);
        return NULL;
    }
    printf("[MQTT-PUB] Connected!\n");
    
    while (running) {
        pthread_mutex_lock(&lock);
        char payload[256];
        // snprintf(payload, sizeof(payload), 
        //          "{\"pump1\":%d,\"pump2\":%d,\"pump3\":%d,\"timestamp\":%ld}",
        //          current_pump_status.pump1, 
        //          current_pump_status.pump2,
        //          current_pump_status.pump3,
        //          current_pump_status.timestamp);
        snprintf(payload, sizeof(payload), 
         "{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"pump3\":%d,\"pump3_status\":%d,\"timestamp\":%ld}",
         current_pump_status.pump1, current_pump_status.pump1_status,
         current_pump_status.pump2, current_pump_status.pump2_status,
         current_pump_status.pump3, current_pump_status.pump3_status,
         current_pump_status.timestamp);
        pthread_mutex_unlock(&lock);
        
        msg.payload = payload;
        msg.payloadlen = strlen(payload);
        msg.qos = 1;
        msg.retained = 1;
        
        MQTTClient_publishMessage(mqtt_pub_client, "pump/status", &msg, NULL);
        printf("[MQTT-PUB] Published: %s\n", payload);
        
        sleep(5);
    }
    
    MQTTClient_disconnect(mqtt_pub_client, 10000);
    MQTTClient_destroy(&mqtt_pub_client);
    return NULL;
}

void* mqtt_subscriber_thread(void *arg) {
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    
    MQTTClient_create(&mqtt_sub_client, BROKER, "pump_mqtt_sub", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTClient_setCallbacks(mqtt_sub_client, NULL, NULL, mqtt_message_arrived, NULL);
    
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = USERNAME;
    conn_opts.password = PASSWORD;
    
    printf("[MQTT-SUB] Connecting...\n");
    if ((rc = MQTTClient_connect(mqtt_sub_client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("[MQTT-SUB] Failed, rc=%d\n", rc);
        return NULL;
    }
    printf("[MQTT-SUB] Connected!\n");
    
    MQTTClient_subscribe(mqtt_sub_client, "pump/control", 1);
    MQTTClient_subscribe(mqtt_sub_client, "pump/feedback", 1);
    printf("[MQTT-SUB] Subscribed to: pump/control and pump/feedback\n");
    
    while (running) {
        sleep(1);
    }
    
    MQTTClient_disconnect(mqtt_sub_client, 10000);
    MQTTClient_destroy(&mqtt_sub_client);
    return NULL;
}