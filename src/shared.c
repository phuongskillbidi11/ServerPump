#include "shared.h"
#include "db.h"
#include <string.h>
#include <stdio.h>

volatile int running = 1;
pthread_mutex_t lock;
PumpStatus current_pump_status = {0, 0, 0, 0, 0, 0, 0};
PumpHistory pump_history = {0};

void add_pump_history(PumpStatus status) {
    pthread_mutex_lock(&lock);
    
    pump_history.items[pump_history.index] = status;
    pump_history.index = (pump_history.index + 1) % MAX_HISTORY;
    
    if (pump_history.count < MAX_HISTORY) {
        pump_history.count++;
    }
    
    pthread_mutex_unlock(&lock);
}

void update_pump_status(int pump_id, int state) {
    pthread_mutex_lock(&lock);
    
    switch(pump_id) {
        case 1: current_pump_status.pump1 = state; break;
        case 2: current_pump_status.pump2 = state; break;
        case 3: current_pump_status.pump3 = state; break;
    }
    
    current_pump_status.timestamp = time(NULL);
    pthread_mutex_unlock(&lock);
    
    add_pump_history(current_pump_status);
    
    // LƯU VÀO DATABASE
    printf("[DB-INSERT] Saving command pump_id=%d, state=%d\n", pump_id, state);
    db_insert_command(pump_id, state, current_pump_status.timestamp, "api");
    db_insert_snapshot(
        current_pump_status.pump1, current_pump_status.pump1_status,
        current_pump_status.pump2, current_pump_status.pump2_status,
        current_pump_status.pump3, current_pump_status.pump3_status,
        current_pump_status.timestamp
    );
    
    printf("[SHARED] Pump%d COMMAND = %s\n", pump_id, state ? "ON" : "OFF");
}

void update_pump_feedback(int pump_id, int status) {
    pthread_mutex_lock(&lock);
    
    switch(pump_id) {
        case 1: current_pump_status.pump1_status = status; break;
        case 2: current_pump_status.pump2_status = status; break;
        case 3: current_pump_status.pump3_status = status; break;
    }
    
    current_pump_status.timestamp = time(NULL);
    pthread_mutex_unlock(&lock);
    
    // LƯU VÀO DATABASE
    printf("[DB-INSERT] Saving feedback pump_id=%d, status=%d\n", pump_id, status);
    db_insert_feedback(pump_id, status, current_pump_status.timestamp);
    db_insert_snapshot(
        current_pump_status.pump1, current_pump_status.pump1_status,
        current_pump_status.pump2, current_pump_status.pump2_status,
        current_pump_status.pump3, current_pump_status.pump3_status,
        current_pump_status.timestamp
    );
    
    printf("[FEEDBACK] Pump%d actual status = %s\n", pump_id, status ? "RUNNING" : "STOPPED");
}