#include "http_api.h"
#include "mqtt.h"
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <unistd.h>
#include <json-c/json.h>

// POST /api/pump/control
char* handle_pump_control(const char *payload) {
    printf("[API] Control: %s\n", payload);
    
    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = (void*)payload;
    msg.payloadlen = strlen(payload);
    msg.qos = 1;
    msg.retained = 0;
    
    int rc = MQTTClient_publishMessage(mqtt_sub_client, "pump/control", &msg, NULL);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        return strdup("{\"status\":\"error\",\"message\":\"Failed to send\"}");
    }
    
    usleep(100000);
    
    pthread_mutex_lock(&lock);
    static char response[1024];
    snprintf(response, sizeof(response),
             "{\"status\":\"sent\",\"current_state\":{\"pump1\":%d,\"pump2\":%d,\"timestamp\":%ld}}",
             current_pump_status.pump1,
             current_pump_status.pump2,
             current_pump_status.timestamp);
    pthread_mutex_unlock(&lock);
    
    return strdup(response);
}

// POST /api/pump/feedback
int handle_pump_feedback(const char *payload) {
    printf("[API] Feedback: %s\n", payload);
    
    struct json_object *parsed = json_tokener_parse(payload);
    if (!parsed) {
        fprintf(stderr, "[API] Failed to parse feedback JSON\n");
        return 400;
    }
    
    struct json_object *pump_id_obj, *status_obj;
    
    if (json_object_object_get_ex(parsed, "pump_id", &pump_id_obj) &&
        json_object_object_get_ex(parsed, "status", &status_obj)) {
        
        int pump_id = json_object_get_int(pump_id_obj);
        int status = json_object_get_int(status_obj);
        
        update_pump_feedback(pump_id, status);
        json_object_put(parsed);
        return 200;
    }
    
    json_object_put(parsed);
    return 400;
}
// GET /api/gateway/status - Kiểm tra gateway hardware
char* handle_gateway_status() {
    pthread_mutex_lock(&lock);
    
    time_t now = time(NULL);
    long seconds_since_last_seen = now - gateway_hw_status.last_seen_at;
    
    // Nếu > 30s không nhận heartbeat → OFFLINE
    int is_online = (seconds_since_last_seen < 30) && gateway_hw_status.is_online;
    
    static char response[1024];
    snprintf(response, sizeof(response),
             "{"
             "\"status\":%d,"                  
             "\"device_id\":\"%s\","
             "\"firmware\":\"%s\","
             "\"last_seen\":%ld,"
             "\"seconds_since_last_seen\":%ld"
             "}",
             is_online ? 1 : 0,                  
             gateway_hw_status.device_id,
             gateway_hw_status.firmware_version,
             gateway_hw_status.last_seen_at,
             seconds_since_last_seen);
    
    pthread_mutex_unlock(&lock);
    return strdup(response);
}
// GET /api/pump/status
char* handle_pump_status() {
    pthread_mutex_lock(&lock);
    
    static char response[1024];
    snprintf(response, sizeof(response),
             "{"
             "\"pump1\":%d,"
             "\"pump1_status\":%d,"
             "\"pump2\":%d,"
             "\"pump2_status\":%d,"
             "\"busy\":%d,"
             "\"alarm\":%d,"
             "\"timestamp\":%ld"
             "}",
             current_pump_status.pump1,
             current_pump_status.pump1_status,
             current_pump_status.pump2,
             current_pump_status.pump2_status,
             current_pump_status.busy,
             current_pump_status.alarm,
             current_pump_status.timestamp);
    
    pthread_mutex_unlock(&lock);
    return strdup(response);
}

// GET /api/pump/history
char* handle_pump_history() {
    pthread_mutex_lock(&lock);
    
    static char response[102400];
    char *ptr = response;
    int remaining = sizeof(response);
    
    int written = snprintf(ptr, remaining, "{\"count\":%d,\"data\":[", pump_history.count);
    ptr += written;
    remaining -= written;
    
    for (int i = 0; i < pump_history.count && remaining > 100; i++) {
        int idx = (pump_history.index - pump_history.count + i + MAX_HISTORY) % MAX_HISTORY;
        PumpStatus *item = &pump_history.items[idx];
        
        written = snprintf(ptr, remaining,
                          "%s{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"timestamp\":%ld}",
                          (i > 0 ? "," : ""),
                          item->pump1, item->pump1_status,
                          item->pump2, item->pump2_status,
                        //   item->pump3, item->pump3_status,
                          item->timestamp);
        ptr += written;
        remaining -= written;
    }
    
    snprintf(ptr, remaining, "]}");
    pthread_mutex_unlock(&lock);
    
    return strdup(response);
}
static enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                                      const char *url, const char *method,
                                      const char *version, const char *upload_data,
                                      size_t *upload_data_size, void **con_cls) {
    
    printf("[DEBUG] Request: %s %s\n", method, url);
    
    struct MHD_Response *response;
    char *response_data = NULL;
    int status_code = 200;
    
    // Handle OPTIONS (CORS preflight)
    if (strcmp(method, "OPTIONS") == 0) {
        response_data = strdup("");
        response = MHD_create_response_from_buffer(0, response_data, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        MHD_add_response_header(response, "Access-Control-Max-Age", "86400");
        
        enum MHD_Result ret = MHD_queue_response(connection, 204, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // Handle POST data
    if (strcmp(method, "POST") == 0) {
        if (*con_cls == NULL) {
            char *post_buffer = malloc(1024);
            if (!post_buffer) return MHD_NO;
            post_buffer[0] = '\0';
            *con_cls = post_buffer;
            return MHD_YES;
        }
        
        char *post_buffer = *con_cls;
        
        if (*upload_data_size != 0) {
            strncat(post_buffer, upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }
        
        printf("[DEBUG] POST complete, data: %s\n", post_buffer);
        
        if (strcmp(url, "/api/pump/control") == 0) {
            response_data = handle_pump_control(post_buffer);
            status_code = 200;
            
        } else if (strcmp(url, "/api/pump/feedback") == 0) {
            status_code = handle_pump_feedback(post_buffer);
            response_data = strdup("{\"status\":\"received\"}");
            
        } else {
            status_code = 404;
            response_data = strdup("{\"error\":\"Not found\"}");
        }
        free(post_buffer);
    } 
    // Handle GET
    else if (strcmp(method, "GET") == 0) {
        if (strcmp(url, "/api/pump/status") == 0) {
            response_data = handle_pump_status();
            
        } else if (strcmp(url, "/api/pump/history") == 0) {
            response_data = handle_pump_history();
            
        } else if (strcmp(url, "/api/gateway/status") == 0) { 
            response_data = handle_gateway_status();
            
        } else {
            status_code = 404;
            response_data = strdup("{\"error\":\"Not found\"}");
        }
    }
        else {
        status_code = 405;
        response_data = strdup("{\"error\":\"Method not allowed\"}");
    }
    
    printf("[DEBUG] Response: %d\n", status_code);
    
    response = MHD_create_response_from_buffer(strlen(response_data), response_data, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
    MHD_add_response_header(response, "Content-Type", "application/json");
    
    enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

void* http_api_thread(void *arg) {
    struct MHD_Daemon *daemon;
    
    sleep(2);
    
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, HTTP_PORT, NULL, NULL, &handle_request, NULL, MHD_OPTION_END);
    if (!daemon) {
        printf("[HTTP-API] Failed\n");
        return NULL;
    }
    
    printf("[HTTP-API] Running on http://localhost:%d\n", HTTP_PORT);
    printf( "GET http://localhost:%d/api/gateway/status - Check gateway hardware\n", HTTP_PORT);
    printf("  POST http://localhost:%d/api/pump/control  - Send command\n", HTTP_PORT);
    printf("  POST http://localhost:%d/api/pump/feedback - Receive hardware status\n", HTTP_PORT);
    printf("  GET  http://localhost:%d/api/pump/status   - Get current state\n", HTTP_PORT);
    printf("  GET  http://localhost:%d/api/pump/history  - Get history\n", HTTP_PORT);
    
    while (running) {
        sleep(1);
    }
    
    MHD_stop_daemon(daemon);
    return NULL;
}