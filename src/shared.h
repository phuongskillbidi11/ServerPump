#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <time.h>

#define MAX_HISTORY 100
#define BROKER "tcp://vm01.i-soft.com.vn:46183"
#define USERNAME "user1"
#define PASSWORD "OEu9ICmhKtMb4JB0APsaXWqg"

// Pump Status Enum 
#define STATUS_UNKNOWN  0
#define STATUS_RUNNING  1
#define STATUS_STOPPED  2
#define STATUS_ERROR    3

// Busy Status
#define BUSY_IDLE           0
#define BUSY_STARTING_P1    1
#define BUSY_STARTING_P2    2

typedef struct {
    // Software commands
    int pump1;              // 0=OFF, 1=ON
    int pump2;              // 0=OFF, 1=ON
    
    // Hardware feedback (4 states)
    int pump1_status;       // 0=Unknown, 1=Running, 2=Stopped, 3=Error
    int pump2_status;       // 0=Unknown, 1=Running, 2=Stopped, 3=Error
    
    // System status
    int busy;               // 0=Idle, 1=Starting_P1, 2=Starting_P2
    int alarm;              // 0=No_Alarm, 1=Alarm_Active
    
    time_t timestamp;
} PumpStatus;

typedef struct {
    PumpStatus items[MAX_HISTORY];
    int count;
    int index;
} PumpHistory;

typedef struct {
    int is_online;
    int gateway_reported_status;
    time_t last_seen_at;
    char device_id[64];
    char firmware_version[32];
} GatewayHardwareStatus;

// Global
extern volatile int running;
extern pthread_mutex_t lock;
extern PumpStatus current_pump_status;
extern PumpHistory pump_history;
extern GatewayHardwareStatus gateway_hw_status;

void add_pump_history(PumpStatus status);
void update_pump_status(int pump_id, int state);
void update_pump_feedback(int pump_id, int status);
void update_gateway_heartbeat(const char *device_id, const char *firmware, int status);

#endif