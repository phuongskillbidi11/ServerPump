#include "shared.h"
#include "db.h"
#include <string.h>
#include <stdio.h>

volatile int running = 1;
pthread_mutex_t lock;
// PumpStatus current_pump_status = {0, 0, 0, 0, 0, 0, 0};
GatewayHardwareStatus gateway_hw_status = {0, 0, "", ""};
PumpStatus current_pump_status = {0, 0, 0, 0, 0};
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
void update_gateway_heartbeat(const char *device_id, const char *firmware) {
    pthread_mutex_lock(&lock);
    
    gateway_hw_status.is_online = 1;
    gateway_hw_status.last_seen_at = time(NULL);
    
    if (device_id) {
        strncpy(gateway_hw_status.device_id, device_id, sizeof(gateway_hw_status.device_id) - 1);
    }
    
    if (firmware) {
        strncpy(gateway_hw_status.firmware_version, firmware, sizeof(gateway_hw_status.firmware_version) - 1);
    }
    
    pthread_mutex_unlock(&lock);
    
    printf("[GATEWAY] Heartbeat received from %s (FW: %s)\n", 
           device_id ? device_id : "unknown", 
           firmware ? firmware : "unknown");
}

        // Giả lập: status sẽ được cập nhật từ MQTT feedback
        // tạm thời set luôn trạng thái thực tế giống lệnh điều khiển, sau này dùng feedback từ plc | curl -X POST http://localhost:8080/api/pump/feedback   -H "Content-Type: application/json"   -d '{"pump_id":1,"status":1}'

void update_pump_status(int pump_id, int state) {
    pthread_mutex_lock(&lock);
    
    switch(pump_id) {
        case 1: current_pump_status.pump1 = state; break;
        case 2: current_pump_status.pump2 = state; break;
        // case 3: current_pump_status.pump3 = state; break;
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
        current_pump_status.timestamp
    );
    
    printf("[SHARED] Pump%d COMMAND = %s\n", pump_id, state ? "ON" : "OFF");
}

void update_pump_feedback(int pump_id, int status) {
    pthread_mutex_lock(&lock);
    
    switch(pump_id) {
        case 1: current_pump_status.pump1_status = status; break;
        case 2: current_pump_status.pump2_status = status; break;
        // case 3: current_pump_status.pump3_status = status; break;
    }
    
    current_pump_status.timestamp = time(NULL);
    pthread_mutex_unlock(&lock);
    
    // LƯU VÀO DATABASE
    printf("[DB-INSERT] Saving feedback pump_id=%d, status=%d\n", pump_id, status);
    db_insert_feedback(pump_id, status, current_pump_status.timestamp);
    db_insert_snapshot(
        current_pump_status.pump1, current_pump_status.pump1_status,
        current_pump_status.pump2, current_pump_status.pump2_status,
        // current_pump_status.pump3, current_pump_status.pump3_status,
        current_pump_status.timestamp
    );
    
    printf("[FEEDBACK] Pump%d actual status = %s\n", pump_id, status ? "RUNNING" : "STOPPED");
}