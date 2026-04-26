# API Specification
### Room Audio Monitor — FastAPI Backend

---

## Base URL

Local development:
```
http://localhost:8000
```

Render deployment:
```
https://comp3-spectrum-analyzer.onrender.com
```

All endpoints except `GET /ws` require a Bearer token:
```
Authorization: Bearer <API_TOKEN>
```

`API_TOKEN` is set as an environment variable on the server. It must match the `API_TOKEN` constant in the ESP32 firmware and the `API_TOKEN` constant in `index.html`.

---

## Endpoints

### GET /
Serves the frontend dashboard (`frontend/index.html`). No auth required.

---

### POST /data
Ingest a single audio reading from an ESP32.

**Request body**
```json
{
  "device_id": "ESP32_STATION_01",
  "db_level": -42.3,
  "spl": 78.5,
  "bins": [/* exactly 64 floats, dBFS per frequency band */]
}
```

| Field | Type | Description |
|---|---|---|
| `device_id` | string | Unique sensor identifier |
| `db_level` | float | Raw dBFS level (RMS-based, negative) |
| `spl` | float | Calibrated SPL in dB (positive, human-readable) |
| `bins` | array[64 float] | FFT energy per log-spaced band in dBFS |

**Response `200 OK`**
```json
{ "status": "ok", "id": "<mongodb_document_id>" }
```

**Side effects**
- Inserts into the `readings` MongoDB time-series collection
- Broadcasts the reading to all connected WebSocket clients

---

### GET /history
Return all readings in a time window, sorted oldest-first.

**Query params**

| Param       | Type   | Default | Description                     |
|-------------|--------|---------|---------------------------------|
| `window`    | string | `1h`    | `1h`, `6h`, or `24h`            |
| `device_id` | string | —       | Optional — filter to one sensor |

**Response `200 OK`**
```json
[
  {
    "device_id": "ESP32_STATION_01",
    "db_level": -42.3,
    "bins": [...],
    "timestamp": "2025-04-13T14:00:00.000000"
  }
]
```

---

### GET /stats
Return aggregate dB statistics over a time window.

**Query params**

| Param       | Type   | Default | Description                     |
|-------------|--------|---------|---------------------------------|
| `window`    | string | `1h`    | `1h`, `6h`, or `24h`            |
| `device_id` | string | —       | Optional — filter to one sensor |

**Response `200 OK`**
```json
{
  "min_db": -72.1,
  "max_db": -18.4,
  "avg_db": -45.6,
  "count": 3600,
  "window": "1h"
}
```

If no data exists in the window, all values are `null` and `count` is `0`.

---

### POST /sessions
Start a named recording session.

**Request body**
```json
{ "name": "Sunday Service" }
```

**Response `200 OK`**
```json
{ "id": "<session_id>", "name": "Sunday Service" }
```

---

### PUT /sessions/{session_id}/end
End an active session. Sets `end_time` to now.

**Response `200 OK`**
```json
{ "status": "ok" }
```

**Response `404`** if session ID not found.

---

### GET /sessions
List all sessions, sorted newest-first (max 100).

**Response `200 OK`**
```json
[
  {
    "id": "<session_id>",
    "name": "Sunday Service",
    "start_time": "2025-04-13T14:00:00.000000",
    "end_time": "2025-04-13T16:30:00.000000"
  }
]
```

`end_time` is `null` for in-progress sessions.

---

### GET /sessions/{session_id}/average
Return the time-averaged spectrum for a session, grouped by device.

**Response `200 OK`**
```json
{
  "id": "<session_id>",
  "name": "Sunday Service",
  "start_time": "2025-04-13T14:00:00.000000",
  "end_time": "2025-04-13T16:30:00.000000",
  "devices": [
    {
      "device_id": "ESP32_STATION_01",
      "count": 9000,
      "avg_db": -38.4,
      "avg_bins": [/* 64 averaged dBFS values */]
    }
  ]
}
```

If the session has no end time, `end_time` defaults to now for the average calculation.

---

### WebSocket GET /ws
Real-time push endpoint. No auth required (read-only broadcast).

Connect to:
```
ws://localhost:8000/ws          # local
wss://your-service.onrender.com/ws  # Render (must use wss://)
```

**Server → client** (sent on every POST /data):
```json
{
  "device_id": "ESP32_STATION_01",
  "db_level": -42.3,
  "spl": 78.5,
  "bins": [...],
  "timestamp": "2025-04-13T14:00:01.123456"
}
```

**Client → server:** send any text as a keep-alive ping. Content is ignored.

The frontend reconnects automatically every 3 seconds on disconnect.

---

## Error Responses

| Code | Meaning                           |
|------|-----------------------------------|
| 401  | Missing or invalid Bearer token   |
| 404  | Session not found                 |
| 422  | Request body failed validation    |
| 500  | Unexpected server error           |
