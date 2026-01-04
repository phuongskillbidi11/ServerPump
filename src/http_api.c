#include "http_api.h"
#include "mqtt.h"
#include "shared.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <unistd.h>
#include <json-c/json.h>

// Structure to store query params
typedef struct {
    const char *limit_str;
    const char *from_str;
    const char *to_str;
} QueryParams;

// Iterator callback to collect query parameters
static enum MHD_Result get_query_iterator(void *cls, enum MHD_ValueKind kind,
                                          const char *key, const char *value) {
    QueryParams *params = (QueryParams *)cls;
    
    if (strcmp(key, "limit") == 0) {
        params->limit_str = value;
        printf("[PARSE] ✅ limit=%s\n", value);
    } else if (strcmp(key, "from") == 0) {
        params->from_str = value;
        printf("[PARSE] ✅ from=%s\n", value);
    } else if (strcmp(key, "to") == 0) {
        params->to_str = value;
        printf("[PARSE] ✅ to=%s\n", value);
    }
    
    return MHD_YES;
}

char* handle_pump_control(const char *payload) {
    printf("[API] Control: %s\n", payload);
    
    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = (void*)payload;
    msg.payloadlen = strlen(payload);
    msg.qos = 1;
    msg.retained = 0;
    
    int rc = MQTTClient_publishMessage(mqtt_sub_client, "pump/control", &msg, NULL);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        return strdup("{\"status\":\"error\"}");
    }
    
    usleep(100000);
    
    pthread_mutex_lock(&lock);
    static char response[1024];
    snprintf(response, sizeof(response),
             "{\"status\":\"sent\",\"current_state\":{\"pump1\":%d,\"pump2\":%d}}",
             current_pump_status.pump1, current_pump_status.pump2);
    pthread_mutex_unlock(&lock);
    
    return strdup(response);
}

int handle_pump_feedback(const char *payload) {
    printf("[API] Feedback: %s\n", payload);
    
    struct json_object *parsed = json_tokener_parse(payload);
    if (!parsed) return 400;
    
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

char* handle_gateway_status() {
    pthread_mutex_lock(&lock);
    
    time_t now = time(NULL);
    long seconds_since_last_seen = now - gateway_hw_status.last_seen_at;
    int is_online = (seconds_since_last_seen < 30) && gateway_hw_status.is_online;
    
    static char response[1024];
    snprintf(response, sizeof(response),
             "{\"status\":%d,\"is_online\":%d,\"device_id\":\"%s\",\"firmware\":\"%s\",\"last_seen\":%ld}",
             gateway_hw_status.gateway_reported_status,  
             is_online,                                  
             gateway_hw_status.device_id,
             gateway_hw_status.firmware_version, 
             gateway_hw_status.last_seen_at);
    
    pthread_mutex_unlock(&lock);
    return strdup(response);
}

char* handle_pump_status() {
    pthread_mutex_lock(&lock);
    
    static char response[1024];
    snprintf(response, sizeof(response),
             "{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"busy\":%d,\"alarm\":%d,\"timestamp\":%ld}",
             current_pump_status.pump1, current_pump_status.pump1_status,
             current_pump_status.pump2, current_pump_status.pump2_status,
             current_pump_status.busy, current_pump_status.alarm,
             current_pump_status.timestamp);
    
    pthread_mutex_unlock(&lock);
    return strdup(response);
}

char* handle_pump_history(struct MHD_Connection *connection) {
    static char response[512000];
    
    // Initialize query params structure
    QueryParams params = {NULL, NULL, NULL};
    
    // Extract query parameters from connection
    MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, get_query_iterator, &params);
    
    // Parse parameters with defaults
    int limit = params.limit_str ? atoi(params.limit_str) : 1000;
    time_t from = params.from_str ? (time_t)atoll(params.from_str) : 0;
    time_t to = params.to_str ? (time_t)atoll(params.to_str) : 0;
    
    if (limit > 5000) limit = 5000;
    if (limit < 1) limit = 1000;
    
    printf("[API] ✅ FINAL PARAMS: limit=%d, from=%ld, to=%ld\n", limit, from, to);
    
    int result = db_get_history_filtered(response, sizeof(response), limit, from, to);
    
    if (result != 0) {
        return strdup("{\"error\":\"Database failed\"}");
    }
    
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
    
    if (strcmp(method, "OPTIONS") == 0) {
        response_data = strdup("");
        response = MHD_create_response_from_buffer(0, response_data, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        
        enum MHD_Result ret = MHD_queue_response(connection, 204, response);
        MHD_destroy_response(response);
        return ret;
    }
    
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
        
        if (strcmp(url, "/api/pump/control") == 0) {
            response_data = handle_pump_control(post_buffer);
        } else if (strcmp(url, "/api/pump/feedback") == 0) {
            status_code = handle_pump_feedback(post_buffer);
            response_data = strdup("{\"status\":\"ok\"}");
        } else {
            status_code = 404;
            response_data = strdup("{\"error\":\"Not found\"}");
        }
        free(post_buffer);
    } 
    else if (strcmp(method, "GET") == 0) {
        if (strcmp(url, "/api/pump/status") == 0) {
            response_data = handle_pump_status();
        } else if (strncmp(url, "/api/pump/history", 17) == 0) {
            response_data = handle_pump_history(connection);  
        } else if (strcmp(url, "/api/gateway/status") == 0) { 
            response_data = handle_gateway_status();
        } else {
            status_code = 404;
            response_data = strdup("{\"error\":\"Not found\"}");
        }
    }
    else {
        status_code = 405;
        response_data = strdup("{\"error\":\"Not allowed\"}");
    }
    
    response = MHD_create_response_from_buffer(strlen(response_data), response_data, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
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
    
    printf("[HTTP-API] Running on port %d\n", HTTP_PORT);
    
    while (running) {
        sleep(1);
    }
    
    MHD_stop_daemon(daemon);
    return NULL;
}