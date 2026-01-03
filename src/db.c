#include "db.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

sqlite3 *db = NULL;

int db_init() {
    mkdir("/var/lib/pump_server", 0755);
    
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cannot open: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    printf("[DB] Opened: %s\n", DB_PATH);
    
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS pump_commands (id INTEGER PRIMARY KEY AUTOINCREMENT, pump_id INTEGER, command INTEGER, timestamp INTEGER, source TEXT);"
        "CREATE TABLE IF NOT EXISTS pump_feedback (id INTEGER PRIMARY KEY AUTOINCREMENT, pump_id INTEGER, status INTEGER, timestamp INTEGER);"
        "CREATE TABLE IF NOT EXISTS pump_snapshots (id INTEGER PRIMARY KEY AUTOINCREMENT, pump1_cmd INTEGER, pump1_status INTEGER, pump2_cmd INTEGER, pump2_status INTEGER, busy INTEGER, alarm INTEGER, timestamp INTEGER);"
        "CREATE TABLE IF NOT EXISTS gateway_history (id INTEGER PRIMARY KEY AUTOINCREMENT, is_online INTEGER, device_id TEXT, firmware TEXT, timestamp INTEGER);"
        "CREATE INDEX IF NOT EXISTS idx_snapshots_time ON pump_snapshots(timestamp);";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("[DB] Tables OK\n");
    return 0;
}

int db_close() {
    if (db) {
        sqlite3_close(db);
        printf("[DB] Closed\n");
    }
    return 0;
}

int db_insert_command(int pump_id, int command, time_t timestamp, const char *source) {
    const char *sql = "INSERT INTO pump_commands VALUES (NULL,?,?,?,?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, pump_id);
    sqlite3_bind_int(stmt, 2, command);
    sqlite3_bind_int64(stmt, 3, timestamp);
    sqlite3_bind_text(stmt, 4, source, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_insert_feedback(int pump_id, int status, time_t timestamp) {
    const char *sql = "INSERT INTO pump_feedback VALUES (NULL,?,?,?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, pump_id);
    sqlite3_bind_int(stmt, 2, status);
    sqlite3_bind_int64(stmt, 3, timestamp);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_insert_snapshot(int p1_cmd, int p1_st, int p2_cmd, int p2_st, int busy, int alarm, time_t timestamp) {
    if (!db) return -1;
    
    const char *sql = "INSERT INTO pump_snapshots VALUES (NULL,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, p1_cmd);
    sqlite3_bind_int(stmt, 2, p1_st);
    sqlite3_bind_int(stmt, 3, p2_cmd);
    sqlite3_bind_int(stmt, 4, p2_st);
    sqlite3_bind_int(stmt, 5, busy);
    sqlite3_bind_int(stmt, 6, alarm);
    sqlite3_bind_int64(stmt, 7, timestamp);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_insert_gateway_status(int is_online, const char *device_id, const char *firmware, time_t timestamp) {
    if (!db) return -1;
    
    const char *sql = "INSERT INTO gateway_history VALUES (NULL,?,?,?,?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, is_online);
    sqlite3_bind_text(stmt, 2, device_id ? device_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, firmware ? firmware : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, timestamp);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_history(char *output, int max_size, int limit) {
    if (!db) {
        sprintf(output, "{\"error\":\"DB not init\"}");
        return -1;
    }
    
    const char *sql = "SELECT * FROM pump_snapshots ORDER BY timestamp DESC LIMIT ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sprintf(output, "{\"error\":\"Query failed\"}");
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, limit);
    
    char temp[102400];
    char *ptr = temp;
    int remaining = sizeof(temp);
    int count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && remaining > 200) {
        int written = snprintf(ptr, remaining,
            "%s{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"busy\":%d,\"alarm\":%d,\"timestamp\":%lld}",
            (count > 0 ? "," : ""),
            sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2),
            sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4),
            sqlite3_column_int(stmt, 5), sqlite3_column_int(stmt, 6),
            (long long)sqlite3_column_int64(stmt, 7));
        
        ptr += written;
        remaining -= written;
        count++;
    }
    
    sqlite3_finalize(stmt);
    snprintf(output, max_size, "{\"count\":%d,\"data\":[%s]}", count, temp);
    
    printf("[DB] Retrieved %d records\n", count);
    return 0;
}

int db_get_history_filtered(char *output, int max_size, int limit, time_t from, time_t to) {
    if (!db) {
        sprintf(output, "{\"error\":\"DB not init\"}");
        return -1;
    }
    
    char sql[512];
    sqlite3_stmt *stmt;
    
    printf("[DB-FILTER] CALLED: limit=%d, from=%ld, to=%ld\n", limit, from, to);
    
    // Build SQL
    if (from > 0 && to > 0) {
        snprintf(sql, sizeof(sql), "SELECT * FROM pump_snapshots WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp DESC LIMIT ?");
    } else if (from > 0) {
        snprintf(sql, sizeof(sql), "SELECT * FROM pump_snapshots WHERE timestamp >= ? ORDER BY timestamp DESC LIMIT ?");
    } else if (to > 0) {
        snprintf(sql, sizeof(sql), "SELECT * FROM pump_snapshots WHERE timestamp <= ? ORDER BY timestamp DESC LIMIT ?");
    } else {
        snprintf(sql, sizeof(sql), "SELECT * FROM pump_snapshots ORDER BY timestamp DESC LIMIT ?");
    }
    
    printf("[DB-FILTER] SQL: %s\n", sql);
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB-FILTER] Prepare failed: %s\n", sqlite3_errmsg(db));
        sprintf(output, "{\"error\":\"Prepare failed\"}");
        return -1;
    }
    
    // Bind parameters
    if (from > 0 && to > 0) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)from);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)to);
        sqlite3_bind_int(stmt, 3, limit);
        printf("[DB-FILTER] Bound: from=%ld, to=%ld, limit=%d\n", from, to, limit);
    } else if (from > 0) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)from);
        sqlite3_bind_int(stmt, 2, limit);
        printf("[DB-FILTER] Bound: from=%ld, limit=%d\n", from, limit);
    } else if (to > 0) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)to);
        sqlite3_bind_int(stmt, 2, limit);
        printf("[DB-FILTER] Bound: to=%ld, limit=%d\n", to, limit);
    } else {
        sqlite3_bind_int(stmt, 1, limit);
        printf("[DB-FILTER] Bound: limit=%d\n", limit);
    }
    
    // Build response
    char temp[409600];
    char *ptr = temp;
    int remaining = sizeof(temp);
    int count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && remaining > 200) {
        int written = snprintf(ptr, remaining,
            "%s{\"pump1\":%d,\"pump1_status\":%d,\"pump2\":%d,\"pump2_status\":%d,\"busy\":%d,\"alarm\":%d,\"timestamp\":%lld}",
            (count > 0 ? "," : ""),
            sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2),
            sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4),
            sqlite3_column_int(stmt, 5), sqlite3_column_int(stmt, 6),
            (long long)sqlite3_column_int64(stmt, 7));
        
        ptr += written;
        remaining -= written;
        count++;
    }
    
    sqlite3_finalize(stmt);
    snprintf(output, max_size, "{\"count\":%d,\"data\":[%s]}", count, temp);
    
    printf("[DB-FILTER] ✅ Retrieved %d records\n", count);
    return 0;
}

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
    
    printf("[DB] Cleaned %d days\n", days);
    return 0;
}