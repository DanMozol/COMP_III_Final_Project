# API Specification
### Room Audio Monitor — FastAPI Backend

---

## Base URL

```
http://<server>:8000
```

All endpoints except `GET /ws` require a Bearer token in the `Authorization` header:

```
Authorization: Bearer <API_TOKEN>
```

---

## Endpoints

### POST /data
Ingest a single audio reading from an ESP32 sensor.

**Request body**
```json
{
  "device_id": "ESP32_STATION_01",
  "db_level": -42.3,
  "bins": [/* 64 floats, dBFS per frequency band */]
}
```

**Response `200 OK`**
```json
{
  "status": "ok",
  "id": "<mongodb_document_id>"
}
```

**Side effects**
- Inserts the reading into the MongoDB `readings` time-series collection.
- Broadcasts the reading as JSON to all connected WebSocket clients.

---

### GET /history
Return all readings in a time window, sorted oldest-first.

**Query params**

| Param       | Type   | Default | Description                        |
|-------------|--------|---------|------------------------------------|
| `window`    | string | `1h`    | `1h`, `6h`, or `24h`               |
| `device_id` | string | —       | Optional — filter to one sensor    |

**Response `200 OK`**
```json
[
  {
    "device_id": "ESP32_STATION_01",
    "db_level": -42.3,
    "bins": [...],
    "timestamp": "2025-04-13T14:00:00.000000"
  },
  ...
]
```

---

### GET /stats
Return aggregate dB statistics over a time window.

**Query params**

| Param       | Type   | Default | Description                        |
|-------------|--------|---------|------------------------------------|
| `window`    | string | `1h`    | `1h`, `6h`, or `24h`               |
| `device_id` | string | —       | Optional — filter to one sensor    |

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

### WebSocket GET /ws
Real-time push endpoint. No auth required (read-only).

Connect to:
```
ws://<server>:8000/ws
```

**Server → client message** (sent on every new POST /data)
```json
{
  "device_id": "ESP32_STATION_01",
  "db_level": -42.3,
  "bins": [...],
  "timestamp": "2025-04-13T14:00:01.123456"
}
```

**Client → server**: send any text to act as a keep-alive ping. Content is ignored.

---

## Error responses

| Code | Meaning                           |
|------|-----------------------------------|
| 401  | Missing or invalid Bearer token   |
| 422  | Request body failed validation    |
| 500  | Unexpected server error           |
