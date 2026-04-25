# Database Specification
### Room Audio Monitor — MongoDB Atlas

---

## Overview

MongoDB Atlas (free tier M0 cluster) is the database. Two collections are used:
- `readings` — time-series collection for audio data, auto-expires after 30 days
- `sessions` — named recording sessions with start/end times

Both are created automatically by `main.py` on server startup.

---

## Atlas Setup

1. Create a free M0 cluster at [cloud.mongodb.com](https://cloud.mongodb.com)
2. Create a database user with read/write access
3. Under **Network Access**, add `0.0.0.0/0` to allow connections from any IP (required for Render deployment)
4. Get your connection string from **Connect → Drivers**:
   ```
   mongodb+srv://<user>:<pass>@<cluster>.mongodb.net/?retryWrites=true&w=majority
   ```
5. Set this as the `MONGO_URI` environment variable on your server

---

## Collection: `readings`

**Type:** Native MongoDB Time-Series Collection

Auto-created on startup:
```python
await db.create_collection(
    "readings",
    timeseries={
        "timeField": "timestamp",
        "metaField": "device_id",
        "granularity": "seconds",
    },
    expireAfterSeconds=60 * 60 * 24 * 30   # 30 days
)
```

Time-series collections use columnar storage internally — much better query and storage efficiency than a regular collection for sequential timestamped data.

### Document Schema

```json
{
  "device_id": "ESP32_STATION_01",
  "db_level":  -42.3,
  "bins": [-60.1, -58.4, -55.2, "...64 values total..."],
  "timestamp": "2025-04-13T14:00:01Z"
}
```

| Field       | Type            | Description                                |
|-------------|-----------------|--------------------------------------------|
| `device_id` | string          | Sensor identifier (metaField)              |
| `db_level`  | float           | Overall room loudness in dBFS              |
| `bins`      | array[64 float] | FFT energy per log-spaced band in dBFS     |
| `timestamp` | datetime (UTC)  | When the reading was captured (timeField)  |

### Indexes

```python
await collection.create_index([("device_id", 1), ("timestamp", -1)])
```

Speeds up the most common query: readings for a specific device within a time window.

---

## Collection: `sessions`

**Type:** Regular collection (no TTL — sessions are kept indefinitely)

### Document Schema

```json
{
  "_id":        ObjectId("..."),
  "name":       "Sunday Service",
  "start_time": "2025-04-13T14:00:00Z",
  "end_time":   "2025-04-13T16:30:00Z"
}
```

`end_time` is `null` while a session is in progress.

### Index

```python
await sessions_col.create_index([("start_time", -1)])
```

---

## Common Query Patterns

### Fetch last N hours of readings
```python
since = datetime.utcnow() - timedelta(hours=N)
collection.find({"timestamp": {"$gte": since}, "device_id": "ESP32_STATION_01"})
           .sort("timestamp", 1)
```

### Aggregate dB stats
```python
[
  {"$match": {"timestamp": {"$gte": since}}},
  {"$group": {
      "_id":    None,
      "min_db": {"$min": "$db_level"},
      "max_db": {"$max": "$db_level"},
      "avg_db": {"$avg": "$db_level"},
      "count":  {"$sum": 1},
  }}
]
```

### Session average (all readings within session window)
```python
collection.find({
    "timestamp": {"$gte": session["start_time"], "$lte": session["end_time"]}
})
```

---

## Environment Variables

```
MONGO_URI=mongodb+srv://<user>:<pass>@<cluster>.mongodb.net/?retryWrites=true&w=majority
MONGO_DB_NAME=audio_spectrum
```

These go in `backend/.env` for local dev, and as env vars in Render for deployment.
