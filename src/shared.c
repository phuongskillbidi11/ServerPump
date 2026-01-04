#include "shared.h"
#include "db.h"
#include <string.h>
#include <stdio.h>

volatile int running = 1;
pthread_mutex_t lock;
PumpStatus current_pump_status = {0, 0, 0, 0, 0, 0, 0};  // 7 giá trị
PumpHistory pump_history = {0};
GatewayHardwareStatus gateway_hw_status = {0, 0, 0, "", ""};

// Previous states for change detection
static PumpStatus previous_pump_status = {0, 0, 0, 0, 0, 0, 0};
static GatewayHardwareStatus previous_gateway_status = {0, 0, 0, "", ""};

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
    
    int previous_state = (pump_id == 1) ? previous_pump_status.pump1 : previous_pump_status.pump2;
    
    switch(pump_id) {
        case 1: current_pump_status.pump1 = state; break;
        case 2: current_pump_status.pump2 = state; break;
    }
    
    current_pump_status.timestamp = time(NULL);
    
    // Check if command actually changed
    int command_changed = (previous_state != state);
    
    pthread_mutex_unlock(&lock);
    
    if (command_changed) {
        add_pump_history(current_pump_status);
        
        printf("[SHARED] Pump%d COMMAND = %s (CHANGED)\n", pump_id, state ? "ON" : "OFF");
        
        db_insert_command(pump_id, state, current_pump_status.timestamp, "api");
        db_insert_snapshot(
            current_pump_status.pump1, current_pump_status.pump1_status,
            current_pump_status.pump2, current_pump_status.pump2_status,
            current_pump_status.busy, current_pump_status.alarm,
            current_pump_status.timestamp
        );
        
        // Update previous state
        pthread_mutex_lock(&lock);
        switch(pump_id) {
            case 1: previous_pump_status.pump1 = state; break;
            case 2: previous_pump_status.pump2 = state; break;
        }
        pthread_mutex_unlock(&lock);
    } else {
        printf("[SHARED] Pump%d COMMAND = %s (no change, skip DB)\n", pump_id, state ? "ON" : "OFF");
    }
}

void update_pump_feedback(int pump_id, int status) {
    pthread_mutex_lock(&lock);
    
    // Validate status (0-3)
    if (status < 0 || status > 3) {
        status = STATUS_UNKNOWN;
    }
    
    int previous_status = (pump_id == 1) ? previous_pump_status.pump1_status : previous_pump_status.pump2_status;
    
    switch(pump_id) {
        case 1: current_pump_status.pump1_status = status; break;
        case 2: current_pump_status.pump2_status = status; break;
    }
    
    current_pump_status.timestamp = time(NULL);
    
    // Check if status actually changed
    int status_changed = (previous_status != status);
    
    pthread_mutex_unlock(&lock);
    
    const char *status_str[] = {"Unknown", "Running", "Stopped", "Error"};
    
    if (status_changed) {
        printf("[FEEDBACK] Pump%d HW Status = %s (CHANGED)\n", pump_id, status_str[status]);
        
        db_insert_feedback(pump_id, status, current_pump_status.timestamp);
        db_insert_snapshot(
            current_pump_status.pump1, current_pump_status.pump1_status,
            current_pump_status.pump2, current_pump_status.pump2_status,
            current_pump_status.busy, current_pump_status.alarm,
            current_pump_status.timestamp
        );
        
        // Update previous state
        pthread_mutex_lock(&lock);
        switch(pump_id) {
            case 1: previous_pump_status.pump1_status = status; break;
            case 2: previous_pump_status.pump2_status = status; break;
        }
        pthread_mutex_unlock(&lock);
    } else {
        printf("[FEEDBACK] Pump%d HW Status = %s (no change, skip DB)\n", pump_id, status_str[status]);
    }
}
void update_gateway_heartbeat(const char *device_id, const char *firmware, int status) {
    pthread_mutex_lock(&lock);
    
    // Check what changed
    int is_first_heartbeat = (gateway_hw_status.last_seen_at == 0);
    int status_changed = (gateway_hw_status.gateway_reported_status != status);
    int online_state_changed = (gateway_hw_status.is_online != 1);
    int firmware_changed = (firmware && strcmp(gateway_hw_status.firmware_version, firmware) != 0);
    
    // Update current state
    gateway_hw_status.is_online = 1;
    gateway_hw_status.gateway_reported_status = status;
    gateway_hw_status.last_seen_at = time(NULL);
    
    if (device_id) {
        strncpy(gateway_hw_status.device_id, device_id, sizeof(gateway_hw_status.device_id) - 1);
    }
    
    if (firmware) {
        strncpy(gateway_hw_status.firmware_version, firmware, sizeof(gateway_hw_status.firmware_version) - 1);
    }
    
    pthread_mutex_unlock(&lock);
    
    // Only save to DB if something important changed
    if (is_first_heartbeat || status_changed || online_state_changed || firmware_changed) {
        printf("[GATEWAY] Heartbeat: %s (FW: %s, Status: %d) - CHANGED, saving to DB\n", 
               device_id ? device_id : "unknown", 
               firmware ? firmware : "unknown",
               status);
        
        db_insert_gateway_status(1, device_id, firmware, gateway_hw_status.last_seen_at);
        
        // Update previous state
        pthread_mutex_lock(&lock);
        previous_gateway_status.is_online = 1;
        previous_gateway_status.gateway_reported_status = status;
        if (device_id) {
            strncpy(previous_gateway_status.device_id, device_id, sizeof(previous_gateway_status.device_id) - 1);
        }
        if (firmware) {
            strncpy(previous_gateway_status.firmware_version, firmware, sizeof(previous_gateway_status.firmware_version) - 1);
        }
        pthread_mutex_unlock(&lock);
    } else {
        printf("[GATEWAY] Heartbeat: %s (FW: %s, Status: %d) - no change, skip DB\n", 
               device_id ? device_id : "unknown", 
               firmware ? firmware : "unknown",
               status);
    }
}

void update_system_status(int busy, int alarm) {
    pthread_mutex_lock(&lock);
    
    int busy_changed = (previous_pump_status.busy != busy);
    int alarm_changed = (previous_pump_status.alarm != alarm);
    
    current_pump_status.busy = busy;
    current_pump_status.alarm = alarm;
    current_pump_status.timestamp = time(NULL);
    
    pthread_mutex_unlock(&lock);
    
    if (busy_changed || alarm_changed) {
        const char *busy_str[] = {"Idle", "Starting_P1", "Starting_P2"};
        
        if (busy_changed) {
            printf("[SYSTEM] Busy status: %s (CHANGED)\n", 
                   (busy >= 0 && busy <= 2) ? busy_str[busy] : "Invalid");
        }
        
        if (alarm_changed) {
            printf("[SYSTEM] Alarm status: %s (CHANGED)\n", alarm ? "ACTIVE" : "Clear");
        }
        
        db_insert_snapshot(
            current_pump_status.pump1, current_pump_status.pump1_status,
            current_pump_status.pump2, current_pump_status.pump2_status,
            current_pump_status.busy, current_pump_status.alarm,
            current_pump_status.timestamp
        );
        
        // Update previous state
        pthread_mutex_lock(&lock);
        previous_pump_status.busy = busy;
        previous_pump_status.alarm = alarm;
        pthread_mutex_unlock(&lock);
    } else {
        printf("[SYSTEM] Busy/Alarm status unchanged, skip DB\n");
    }
}