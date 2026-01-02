#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <time.h>

// Database path
#define DB_PATH "/var/lib/pump_server/pump.db"

// Functions
int db_init();
int db_close();

// Insert
int db_insert_command(int pump_id, int command, time_t timestamp, const char *source);
int db_insert_feedback(int pump_id, int status, time_t timestamp);
// int db_insert_snapshot(int p1_cmd, int p1_st, int p2_cmd, int p2_st, time_t timestamp);
int db_insert_snapshot(int p1_cmd, int p1_st, int p2_cmd, int p2_st, int busy, int alarm, time_t timestamp);

// Query
int db_get_history(char *output, int max_size, int limit);
int db_get_pump_history(int pump_id, char *output, int max_size, int limit);

// Cleanup
int db_cleanup_old_records(int days);

// Global
extern sqlite3 *db;

#endif