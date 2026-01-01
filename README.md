# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A multi-threaded C server application for controlling and monitoring pumps via MQTT and HTTP. The system manages 3 pumps with bidirectional communication: commands are sent via HTTP/MQTT, and hardware feedback is received via MQTT. All state changes are persisted to SQLite.

## Build and Run

Build the server:
```bash
make
```

Clean build artifacts:
```bash
make clean
```

Run the server:
```bash
make run
# or directly:
./build/server
```

## Dependencies

Required system libraries (must be installed):
- `libpaho-mqtt3c` - MQTT client
- `libmicrohttpd` - HTTP server
- `libjson-c` - JSON parsing
- `libsqlite3` - Database
- `pthread` - Multi-threading

## Architecture

### Thread Model

The server runs 3 independent threads spawned in `main.c:29-31`:

1. **MQTT Publisher** (`mqtt_publisher_thread`): Publishes pump state to `pump/status` every 5 seconds
2. **MQTT Subscriber** (`mqtt_subscriber_thread`): Listens to `pump/control` (commands) and `pump/feedback` (hardware status)
3. **HTTP API** (`http_api_thread`): Serves REST endpoints on port 8080

All threads share the global `current_pump_status` protected by `pthread_mutex_t lock`.

### Data Flow

**Command Flow (HTTP → MQTT → Hardware):**
1. HTTP POST to `/api/pump/control` → `http_api.c:handle_pump_control`
2. Publishes JSON to MQTT topic `pump/control` → `http_api.c:21`
3. MQTT subscriber receives it → `mqtt.c:mqtt_message_arrived`
4. Updates shared state → `shared.c:update_pump_status`
5. Records to DB (commands + snapshot tables) → `db.c:db_insert_command`

**Feedback Flow (Hardware → MQTT → Server):**
1. Hardware publishes to `pump/feedback` topic
2. MQTT subscriber receives → `mqtt.c:mqtt_message_arrived`
3. Updates feedback status → `shared.c:update_pump_feedback`
4. Records to DB (feedback + snapshot tables) → `db.c:db_insert_feedback`

### State Management

- **Command state** (`pump1`, `pump2`, `pump3`): What the server told the pump to do (0=OFF, 1=ON)
- **Feedback status** (`pump1_status`, `pump2_status`, `pump3_status`): Actual hardware state reported back
- Both states are maintained separately in `PumpStatus` struct (`shared.h:12-20`)
- History is maintained in circular buffer `PumpHistory` (max 100 entries, `shared.h:23-26`)

### Database Schema

**Tables:**
- `pump_commands`: Individual commands sent (pump_id, command, timestamp, source)
- `pump_feedback`: Hardware status reports (pump_id, status, timestamp)
- `pump_snapshots`: Complete system state snapshots (all 6 values + timestamp)

Database location: `/var/lib/pump_server/pump.db`

Snapshots are created on every state change (both commands and feedback) to maintain a complete timeline.

## HTTP API Endpoints

All endpoints return JSON. Server runs on `http://localhost:8080`.

**POST /api/pump/control**
- Send pump command
- Payload: `{"pump_id": 1, "state": 1}`
- Response: `{"status":"sent","current_state":{...}}`

**POST /api/pump/feedback**
- Receive hardware status (typically called by hardware, not users)
- Payload: `{"pump_id": 1, "status": 1}`
- Response: `{"status":"received"}`

**GET /api/pump/status**
- Get current pump states
- Response: `{"pump1":0,"pump1_status":0,"pump2":0,"pump2_status":0,"pump3":0,"pump3_status":0,"timestamp":...}`

**GET /api/pump/history**
- Get in-memory history (last 100 state changes)
- Response: `{"count":N,"data":[...]}`

## MQTT Configuration

**Broker:** `tcp://localhost:1883`
**Credentials:** admin / 123456 (hardcoded in `shared.h:9-10`)

**Topics:**
- `pump/status` - Published by server every 5s (full state broadcast)
- `pump/control` - Commands to hardware (subscribed by server)
- `pump/feedback` - Hardware status reports (subscribed by server)

**Client IDs:**
- Publisher: `pump_mqtt_pub`
- Subscriber: `pump_mqtt_sub`

## Code Organization

- `main.c` - Entry point, thread initialization, signal handling
- `shared.c/h` - Global state, mutex, status update functions
- `mqtt.c/h` - MQTT publisher/subscriber threads and message handlers
- `http_api.c/h` - HTTP server and endpoint handlers using libmicrohttpd
- `db.c/h` - SQLite database initialization and CRUD operations

## Important Implementation Notes

- Thread synchronization uses a single global mutex (`lock`) for all shared state access
- MQTT messages use QoS 1 (at least once delivery)
- Status publishing uses retained messages (`msg.retained = 1` in `mqtt.c:90`)
- HTTP API uses CORS headers (Access-Control-Allow-Origin: *)
- Database directory `/var/lib/pump_server/` is created automatically with 0755 permissions
- The server handles SIGINT/SIGTERM for graceful shutdown (sets `running = 0`)
