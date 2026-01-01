#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <time.h>

#define MAX_HISTORY 100
#define BROKER "tcp://localhost:1883"
#define USERNAME "admin"
#define PASSWORD "123456"

typedef struct {
    int pump1;  // 0=OFF, 1=ON
    int pump1_status; // trạng thái thực tế 
    int pump2;
    int pump2_status; // trạng thái thực tế
    int pump3;
    int pump3_status; // trạng thái thực tế
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
extern PumpStatus current_pump_status;
extern PumpHistory pump_history;

void add_pump_history(PumpStatus status);
void update_pump_status(int pump_id, int state);
void update_pump_feedback(int pump_id, int status);

#endif