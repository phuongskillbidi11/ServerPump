#include "db.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

sqlite3 *db = NULL;

// Tạo database và tables
int db_init() {
    // Tạo thư mục nếu chưa có
    mkdir("/var/lib/pump_server", 0755);
    
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    printf("[DB] Database opened: %s\n", DB_PATH);
    
    // Tạo tables
    const char *sql_commands = 
        "CREATE TABLE IF NOT EXISTS pump_commands ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  pump_id INTEGER NOT NULL,"
        "  command INTEGER NOT NULL,"
        "  timestamp INTEGER NOT NULL,"
        "  source TEXT DEFAULT 'api'"
        ");"
        
        "CREATE TABLE IF NOT EXISTS pump_feedback ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  pump_id INTEGER NOT NULL,"
        "  status INTEGER NOT NULL,"
        "  timestamp INTEGER NOT NULL"
        ");"
        
        "CREATE TABLE IF NOT EXISTS pump_snapshots ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  pump1_cmd INTEGER DEFAULT 0,"
        "  pump1_status INTEGER DEFAULT 0,"
        "  pump2_cmd INTEGER DEFAULT 0,"
        "  pump2_status INTEGER DEFAULT 0,"
        "  pump3_cmd INTEGER DEFAULT 0,"
        "  pump3_status INTEGER DEFAULT 0,"
        "  timestamp INTEGER NOT NULL"
        ");"
        
        "CREATE INDEX IF NOT EXISTS idx_commands_time ON pump_commands(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_feedback_time ON pump_feedback(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_snapshots_time ON pump_snapshots(timestamp);";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql_commands, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("[DB] Tables created successfully\n");
    return 0;
}

int db_close() {
    if (db) {
        sqlite3_close(db);
        printf("[DB] Database closed\n");
    }
    return 0;
}

// Insert command
int db_insert_command(int pump_id, int command, time_t timestamp, const char *source) {
    const char *sql = "INSERT INTO pump_commands (pump_id, command, timestamp, source) VALUES (?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] Failed to prepare: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, pump_id);
    sqlite3_bind_int(stmt, 2, command);
    sqlite3_bind_int64(stmt, 3, timestamp);
    sqlite3_bind_text(stmt, 4, source, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// Insert feedback
int db_insert_feedback(int pump_id, int status, time_t timestamp) {
    const char *sql = "INSERT INTO pump_feedback (pump_id, status, timestamp) VALUES (?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, pump_id);
    sqlite3_bind_int(stmt, 2, status);
    sqlite3_bind_int64(stmt, 3, timestamp);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// Insert snapshot
int db_insert_snapshot(int p1_cmd, int p1_st, int p2_cmd, int p2_st, time_t timestamp) {
    const char *sql = "INSERT INTO pump_snapshots (pump1_cmd, pump1_status, pump2_cmd, pump2_status, timestamp) "
                      "VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, p1_cmd);
    sqlite3_bind_int(stmt, 2, p1_st);
    sqlite3_bind_int(stmt, 3, p2_cmd);
    sqlite3_bind_int(stmt, 4, p2_st);
    // sqlite3_bind_int(stmt, 5, p3_cmd);
    // sqlite3_bind_int(stmt, 6, p3_st);
    sqlite3_bind_int64(stmt, 7, timestamp);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// Get history (JSON format)
int db_get_history(char *output, int max_size, int limit) {
    const char *sql = "SELECT pump1_cmd, pump1_status, pump2_cmd, pump2_status, timestamp "
                      "FROM pump_snapshots ORDER BY timestamp DESC LIMIT ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, limit);
    
    char *ptr = output;
    int remaining = max_size;
    int count = 0;
    
    ptr += snprintf(ptr, remaining, "{\"count\":0,\"data\":[");
    remaining = max_size - (ptr - output);
    
    while (sqlite3_step(stmt) == SQLITE_ROW && remaining > 200) {
        int written = snprintf(ptr, remaining,
                              "%s{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"timestamp\":%ld}",
                              (count > 0 ? "," : ""),
                              sqlite3_column_int(stmt, 0),
                              sqlite3_column_int(stmt, 1),
                              sqlite3_column_int(stmt, 2),
                              sqlite3_column_int(stmt, 3),
                              sqlite3_column_int(stmt, 4),
                              sqlite3_column_int64(stmt, 5));
        ptr += written;
        remaining -= written;
        count++;
    }
    
    snprintf(ptr, remaining, "]}");
    sqlite3_finalize(stmt);
    
    // Update count
    snprintf(output + 9, 10, "%d", count);
    output[9 + snprintf(NULL, 0, "%d", count)] = ',';
    
    return 0;
}

// Cleanup old records (older than X days)
int db_cleanup_old_records(int days) {
    time_t cutoff = time(NULL) - (days * 86400);
    
    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM pump_snapshots WHERE timestamp < %ld", cutoff);
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cleanup error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("[DB] Cleaned up records older than %d days\n", days);
    return 0;
}