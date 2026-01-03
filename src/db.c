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
        "  pump1_cmd INTEGER,"
        "  pump1_status INTEGER,"      // 0=Unknown, 1=Running, 2=Stopped, 3=Error
        "  pump2_cmd INTEGER,"
        "  pump2_status INTEGER,"      // 0=Unknown, 1=Running, 2=Stopped, 3=Error
        "  busy INTEGER,"              // 0=Idle, 1=Starting_P1, 2=Starting_P2
        "  alarm INTEGER,"             // 0=No, 1=Yes
        "  timestamp INTEGER NOT NULL"
        ");"

        "CREATE TABLE IF NOT EXISTS gateway_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  is_online INTEGER,"
        "  device_id TEXT,"
        "  firmware TEXT,"
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
int db_insert_snapshot(int p1_cmd, int p1_st, int p2_cmd, int p2_st, int busy, int alarm, time_t timestamp) {
    if (!db) {
        fprintf(stderr, "[DB] ERROR: Database not initialized!\n");
        return -1;
    }
    
    const char *sql = "INSERT INTO pump_snapshots VALUES (NULL, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] ERROR preparing snapshot insert: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, p1_cmd);
    sqlite3_bind_int(stmt, 2, p1_st);
    sqlite3_bind_int(stmt, 3, p2_cmd);
    sqlite3_bind_int(stmt, 4, p2_st);
    sqlite3_bind_int(stmt, 5, busy);
    sqlite3_bind_int(stmt, 6, alarm);
    sqlite3_bind_int64(stmt, 7, timestamp);
    
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] ERROR inserting snapshot: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    
    sqlite3_finalize(stmt);
    printf("[DB] ✓ Inserted snapshot\n");
    return 0;
}
int db_insert_gateway_status(int is_online, const char *device_id, const char *firmware, time_t timestamp) {
    if (!db) {
        fprintf(stderr, "[DB] ERROR: Database not initialized!\n");
        return -1;
    }
    
    const char *sql = "INSERT INTO gateway_history VALUES (NULL, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] ERROR preparing gateway insert: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, is_online);
    sqlite3_bind_text(stmt, 2, device_id ? device_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, firmware ? firmware : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, timestamp);
    
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] ERROR inserting gateway status: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    
    sqlite3_finalize(stmt);
    printf("[DB] ✓ Inserted gateway status\n");
    return 0;
}
// Get history (JSON format)
int db_get_history(char *output, int max_size, int limit) {
    if (!db) {
        sprintf(output, "{\"error\":\"Database not initialized\"}");
        return -1;
    }
    
    const char *sql = "SELECT * FROM pump_snapshots ORDER BY timestamp DESC LIMIT ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB] ERROR preparing history query: %s\n", sqlite3_errmsg(db));
        sprintf(output, "{\"error\":\"Query failed\"}");
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, limit);
    
    char temp_data[102400];
    char *ptr = temp_data;
    int remaining = sizeof(temp_data);
    int count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && remaining > 200) {
        int written = snprintf(ptr, remaining,
            "%s{"
            "\"pump1\":%d,"
            "\"pump1_status\":%d,"
            "\"pump2\":%d,"
            "\"pump2_status\":%d,"
            "\"busy\":%d,"
            "\"alarm\":%d,"
            "\"timestamp\":%lld"
            "}",
            (count > 0 ? "," : ""),
            sqlite3_column_int(stmt, 1),
            sqlite3_column_int(stmt, 2),
            sqlite3_column_int(stmt, 3),
            sqlite3_column_int(stmt, 4),
            sqlite3_column_int(stmt, 5),
            sqlite3_column_int(stmt, 6),
            (long long)sqlite3_column_int64(stmt, 7)
        );
        
        ptr += written;
        remaining -= written;
        count++;
    }
    
    sqlite3_finalize(stmt);
    
    // Build final JSON với count ĐÚNG
    snprintf(output, max_size, "{\"count\":%d,\"data\":[%s]}", count, temp_data);
    
    printf("[DB] Retrieved %d history records\n", count);
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