
## Project Overview Hoàng Phương Skillbidi

Multi-threaded C server for controlling and monitoring 2 pumps via MQTT and HTTP. The system has bidirectional communication: commands sent via HTTP/MQTT, hardware feedback received via MQTT, and gateway heartbeat monitoring. All state changes persist to SQLite.

## Build Commands

```bash
# Build the server
make

# Clean build artifacts
make clean

# Run the server
make run
# or directly:
./build/server
```

## Dependencies

Required system libraries:
- `libpaho-mqtt3c` - MQTT client
- `libmicrohttpd` - HTTP server
- `libjson-c` - JSON parsing
- `libsqlite3` - Database
- `pthread` - Multi-threading

## Architecture

### Thread Model (src/main.c:29-31)

Three independent threads spawned at startup:

1. **MQTT Publisher** - Publishes full pump state to `pump/status` every 5 seconds (mqtt.c:142-194)
2. **MQTT Subscriber** - Receives messages on 3 topics: `gateway/heartbeat`, `pump/control`, `pump/feedback` (mqtt.c:196-226)
3. **HTTP API** - Serves REST endpoints on port 8080 (http_api.c:226-250)

All threads share `current_pump_status` and `gateway_hw_status` globals protected by single mutex `lock`.

### State Model

**Pump State (PumpStatus in shared.h:23-37):**
- `pump1`, `pump2`: Command state (0=OFF, 1=ON) - what server told pumps to do
- `pump1_status`, `pump2_status`: Hardware feedback (0=Unknown, 1=Running, 2=Stopped, 3=Error) - actual hardware state
- `busy`: System busy state (0=Idle, 1=Starting_P1, 2=Starting_P2)
- `alarm`: Alarm status (0=Clear, 1=Active)
- `timestamp`: Last update time

**Gateway Hardware Status (GatewayHardwareStatus in shared.h:45-51):**
- `is_online`: Gateway connectivity (updated via heartbeat)
- `device_id`, `firmware_version`: Gateway identification
- `last_seen_at`: Last heartbeat timestamp (used for timeout detection)

### Data Flow

**Command Flow (HTTP → MQTT → Hardware):**
1. HTTP POST `/api/pump/control` with `{"pump_id":1, "state":1}` → http_api.c:12-39
2. Publishes to MQTT `pump/control` topic → http_api.c:21
3. MQTT subscriber receives (loopback) → mqtt.c:50-70
4. Updates shared state → shared.c:update_pump_status()
5. Records to DB: pump_commands + pump_snapshots → db.c:83-101, db.c:123-155

**Feedback Flow (Hardware → MQTT → Server):**
1. Hardware publishes to `pump/feedback` with `{"pump_id":1, "status":1, "busy":0, "alarm":0}`
2. MQTT subscriber receives → mqtt.c:73-132
3. Updates pump feedback, busy, and alarm states → shared.c:update_pump_feedback()
4. Records to DB: pump_feedback + pump_snapshots → db.c:104-120, db.c:123-155

**Gateway Heartbeat Flow:**
1. Gateway publishes to `gateway/heartbeat` with `{"device_id":"...", "firmware":"...", "status":1}`
2. MQTT subscriber receives → mqtt.c:19-47
3. Updates gateway status and last_seen timestamp → shared.c:update_gateway_heartbeat()
4. Records to DB: gateway_history → db.c:156-185
5. GET `/api/gateway/status` checks if last_seen > 30s ago to determine offline status → http_api.c:68-94

### Database Schema (db.c:22-59)

Location: `/var/lib/pump_server/pump.db` (auto-created with 0755 permissions)

**Tables:**
- `pump_commands` - Individual commands (pump_id, command, timestamp, source)
- `pump_feedback` - Hardware status reports (pump_id, status, timestamp)
- `pump_snapshots` - Complete system snapshots (pump1_cmd, pump1_status, pump2_cmd, pump2_status, busy, alarm, timestamp)
- `gateway_history` - Gateway connectivity log (is_online, device_id, firmware, timestamp)

Snapshots are created on every state change to maintain complete timeline.

## HTTP API Endpoints

Server runs on `http://localhost:8080`. All endpoints return JSON with CORS enabled (`*`).

**POST /api/pump/control**
- Send pump command (publishes to MQTT)
- Body: `{"pump_id": 1, "state": 1}`
- Response: `{"status":"sent","current_state":{...}}`

**POST /api/pump/feedback**
- Receive hardware feedback (typically from hardware, not users)
- Body: `{"pump_id": 1, "status": 1, "busy": 0, "alarm": 0}`
- Response: `{"status":"received"}`

**GET /api/pump/status**
- Get current pump state
- Response: `{"pump1":0,"pump1_status":0,"pump2":0,"pump2_status":0,"busy":0,"alarm":0,"timestamp":...}`

**GET /api/gateway/status**
- Check gateway hardware connectivity (offline if last_seen > 30s)
- Response: `{"status":1,"device_id":"...","firmware":"...","last_seen":...,"seconds_since_last_seen":...}`

**GET /api/pump/history**
- Get last 100 snapshots from database
- Response: `{"count":N,"data":[...]}`

## MQTT Configuration

**Broker:** `tcp://vm01.i-soft.com.vn:46183` (shared.h:8)
**Credentials:** user1 / OEu9ICmhKtMb4JB0APsaXWqg (shared.h:9-10)

**Topics:**
- `pump/status` - Server publishes full state every 5s (QoS 1, retained)
- `pump/control` - Commands to hardware (QoS 1, subscribed by server)
- `pump/feedback` - Hardware status (QoS 1, subscribed by server)
- `gateway/heartbeat` - Gateway connectivity (QoS 1, subscribed by server)

**Client IDs:**
- Publisher: `pump_mqtt_pub`
- Subscriber: `pump_mqtt_sub`

## Code Organization

- `main.c` - Entry point, thread spawning, signal handling (SIGINT/SIGTERM)
- `shared.c/h` - Global state, mutex, status update functions
- `mqtt.c/h` - MQTT publisher/subscriber threads, message routing by topic
- `http_api.c/h` - HTTP server using libmicrohttpd, handles OPTIONS for CORS
- `db.c/h` - SQLite operations, snapshot recording, history retrieval

## Important Implementation Details

**Thread Synchronization:**
- Single global mutex `lock` protects all shared state (shared.h:55)
- Always lock before accessing `current_pump_status` or `gateway_hw_status`
- Mutex initialized in main.c:18, destroyed at shutdown

**MQTT Message Handling:**
- Subscriber uses topic-based routing in mqtt_message_arrived() (mqtt.c:11-140)
- Messages must be freed after processing: MQTTClient_freeMessage(), MQTTClient_free()
- Status publishing uses retained flag (mqtt.c:183) so new subscribers get last state

**HTTP POST Handling:**
- libmicrohttpd requires two-phase POST processing (http_api.c:158-190)
- con_cls stores accumulation buffer between calls
- Data arrives in chunks via upload_data, finished when upload_data_size == 0

**Database Snapshots:**
- Created on every state change (both commands and feedback)
- Contains complete state (both pump commands and feedback statuses)
- Indexes on timestamp columns for fast history queries

**Gateway Offline Detection:**
- Gateway considered offline if no heartbeat received in 30 seconds
- Checked in real-time by GET /api/gateway/status (http_api.c:72-75)
- Heartbeat updates last_seen_at timestamp (shared.c)

**State Enumeration:**
- Pump status values: 0=Unknown, 1=Running, 2=Stopped, 3=Error (shared.h:13-16)
- Busy values: 0=Idle, 1=Starting_P1, 2=Starting_P2 (shared.h:19-21)
- Alarm values: 0=Clear, 1=Active

## Testing/Debugging

Check MQTT broker status:
```bash
# Depends on your MQTT broker setup
sudo systemctl status mosquitto  # if using local mosquitto
```

Send test commands:
```bash
# Send pump control command
curl -X POST http://localhost:8080/api/pump/control \
  -H "Content-Type: application/json" \
  -d '{"pump_id":1,"state":1}'

# Simulate hardware feedback
curl -X POST http://localhost:8080/api/pump/feedback \
  -H "Content-Type: application/json" \
  -d '{"pump_id":1,"status":1,"busy":0,"alarm":0}'

# Get current status
curl http://localhost:8080/api/pump/status

# Check gateway health
curl http://localhost:8080/api/gateway/status
```

View database:
```bash
sqlite3 /var/lib/pump_server/pump.db
# or with GUI:
sqlitebrowser /var/lib/pump_server/pump.db
```
