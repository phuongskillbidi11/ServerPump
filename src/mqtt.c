#include "mqtt.h"
#include "shared.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>

MQTTClient mqtt_pub_client;
MQTTClient mqtt_sub_client;

int mqtt_message_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    char payload[1024];
    snprintf(payload, sizeof(payload), "%.*s", (int)message->payloadlen, (char*)message->payload);
    
    printf("[MQTT-SUB] Topic: %s\n", topicName);
    printf("[MQTT-SUB] Payload: %s\n", payload);
    
    // ===== XỬ LÝ GATEWAY HEARTBEAT =====
    if (strcmp(topicName, "gateway/heartbeat") == 0) {
        struct json_object *parsed = json_tokener_parse(payload);
        if (parsed) {
            struct json_object *device_id_obj, *firmware_obj, *status_obj;
            
            const char *device_id = NULL;
            const char *firmware = NULL;
            int status = 1;  // default online
            
            if (json_object_object_get_ex(parsed, "device_id", &device_id_obj)) {
                device_id = json_object_get_string(device_id_obj);
            }
            
            if (json_object_object_get_ex(parsed, "firmware", &firmware_obj)) {
                firmware = json_object_get_string(firmware_obj);
            }
            
            if (json_object_object_get_ex(parsed, "status", &status_obj)) {
                status = json_object_get_int(status_obj);
            }
            
            update_gateway_heartbeat(device_id, firmware, status);
            json_object_put(parsed);
        }
        
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }
    
    // ===== XỬ LÝ PUMP CONTROL =====
    if (strcmp(topicName, "pump/control") == 0) {
        struct json_object *parsed = json_tokener_parse(payload);
        if (parsed) {
            struct json_object *pump_id_obj, *state_obj;
            
            if (json_object_object_get_ex(parsed, "pump_id", &pump_id_obj) &&
                json_object_object_get_ex(parsed, "state", &state_obj)) {
                
                int pump_id = json_object_get_int(pump_id_obj);
                int state = json_object_get_int(state_obj);
                
                update_pump_status(pump_id, state);
            }
            
            json_object_put(parsed);
        }
        
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }
    
    // ===== XỬ LÝ PUMP FEEDBACK (Từ ESP32/Hardware) =====
    if (strcmp(topicName, "pump/feedback") == 0) {
        struct json_object *parsed = json_tokener_parse(payload);
        if (parsed) {
            struct json_object *pump_id_obj, *status_obj, *busy_obj, *alarm_obj;
            
            // Parse pump_id và status (0=Unknown, 1=Running, 2=Stopped, 3=Error)
            if (json_object_object_get_ex(parsed, "pump_id", &pump_id_obj) &&
                json_object_object_get_ex(parsed, "status", &status_obj)) {
                
                int pump_id = json_object_get_int(pump_id_obj);
                int status = json_object_get_int(status_obj);
                
                // Validate status (0-3)
                if (status >= 0 && status <= 3) {
                    update_pump_feedback(pump_id, status);
                } else {
                    printf("[MQTT-SUB] Invalid pump status: %d (must be 0-3)\n", status);
                }
            }
            
            // Parse busy (0=Idle, 1=Starting_P1, 2=Starting_P2)
            if (json_object_object_get_ex(parsed, "busy", &busy_obj)) {
                int busy = json_object_get_int(busy_obj);
                
                if (busy >= 0 && busy <= 2) {
                    pthread_mutex_lock(&lock);
                    current_pump_status.busy = busy;
                    current_pump_status.timestamp = time(NULL);
                    pthread_mutex_unlock(&lock);
                    
                    const char *busy_str[] = {"Idle", "Starting_P1", "Starting_P2"};
                    printf("[MQTT-SUB] Busy status updated: %s\n", busy_str[busy]);
                } else {
                    printf("[MQTT-SUB] Invalid busy value: %d (must be 0-2)\n", busy);
                }
            }
            
            // Parse alarm (0=No, 1=Yes)
            if (json_object_object_get_ex(parsed, "alarm", &alarm_obj)) {
                int alarm = json_object_get_int(alarm_obj);
                
                if (alarm == 0 || alarm == 1) {
                    pthread_mutex_lock(&lock);
                    current_pump_status.alarm = alarm;
                    current_pump_status.timestamp = time(NULL);
                    pthread_mutex_unlock(&lock);
                    
                    printf("[MQTT-SUB] Alarm status: %s\n", alarm ? "ACTIVE" : "Clear");
                } else {
                    printf("[MQTT-SUB] Invalid alarm value: %d (must be 0 or 1)\n", alarm);
                }
            }
            
            json_object_put(parsed);
        }
        
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }
    
    // ===== TOPIC KHÔNG XÁC ĐỊNH =====
    printf("[MQTT-SUB] Unhandled topic: %s\n", topicName);
    
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
        "{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"busy\":%d,\"alarm\":%d,\"timestamp\":%ld}",
        current_pump_status.pump1, current_pump_status.pump1_status,
        current_pump_status.pump2, current_pump_status.pump2_status,
        current_pump_status.busy, current_pump_status.alarm,
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
    MQTTClient_subscribe(mqtt_sub_client, "gateway/heartbeat", 1);
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