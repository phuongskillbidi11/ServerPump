#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <time.h>

#define MAX_HISTORY 100
#define BROKER "tcp://vm01.i-soft.com.vn:46183"  
#define USERNAME "user1"                         
#define PASSWORD "OEu9ICmhKtMb4JB0APsaXWqg"     
typedef struct {
    int is_online;
    time_t last_seen_at;
    char device_id[64];
    char firmware_version[32];  
}GatewayHardwareStatus;

typedef struct {
    int pump1;
    int pump1_status;
    int pump2;
    int pump2_status;
    time_t timestamp;
} PumpStatus;

typedef struct {
    PumpStatus items[MAX_HISTORY];
    int count;
    int index;
} PumpHistory;

// Global
extern volatile int running;
extern pthread_mutex_t lock;
extern GatewayHardwareStatus gateway_hw_status;
extern PumpStatus current_pump_status;
extern PumpHistory pump_history;

void update_gateway_heartbeat(const char *device_id, const char *firmware);
void add_pump_history(PumpStatus status);
void update_pump_status(int pump_id, int state);
void update_pump_feedback(int pump_id, int status);

#endif