// src/main.c
#include "shared.h"
#include "mqtt.h"
#include "http_api.h"
#include "db.h"         
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void signal_handler(int sig) {
    printf("\n[MAIN] Shutting down...\n");
    running = 0;
}

int main() {
    pthread_t mqtt_pub_tid, mqtt_sub_tid, http_tid;
    
    pthread_mutex_init(&lock, NULL);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== Server Starting ===\n");
    
    if (db_init() != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize database\n");
        return 1;
    }
    
    pthread_create(&mqtt_pub_tid, NULL, mqtt_publisher_thread, NULL);
    pthread_create(&mqtt_sub_tid, NULL, mqtt_subscriber_thread, NULL);
    pthread_create(&http_tid, NULL, http_api_thread, NULL);
    
    printf("[MAIN] All threads started\n");
    printf("Press Ctrl+C to stop\n\n");
    
    pthread_join(mqtt_pub_tid, NULL);
    pthread_join(mqtt_sub_tid, NULL);
    pthread_join(http_tid, NULL);
    
    db_close();
    
    pthread_mutex_destroy(&lock);
    printf("[MAIN] Shutdown complete\n");
    
    return 0;
}