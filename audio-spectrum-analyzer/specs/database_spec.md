# Database Specification
### Room Audio Monitor — MongoDB

---

## Overview

MongoDB Atlas is used as the primary database. All audio readings are stored in a **native time-series collection** (`timeseries`) which is optimized by MongoDB for sequential time-based data — it uses columnar storage internally and has much better query and storage efficiency than a regular collection for this workload.

---

## Collection: `readings`

**Type:** MongoDB Time-Series Collection

**Created automatically** by `main.py` on server startup if it doesn't already exist:

```python
await db.create_collection(
    "readings",
    timeseries={
        "timeField": "timestamp",
        "metaField": "device_id",
        "granularity": "seconds",
    },
    expireAfterSeconds=2592000  # 30 days
)
```

---

## Document Schema

```json
{
  "device_id": "ESP32_STATION_01",   // metaField — identifies the sensor
  "db_level":  -42.3,                // overall dBFS reading
  "bins": [                          // 64 log-spaced frequency bands (dBFS each)
    -60.1, -58.4, -55.2, ...         // index 0 = ~20Hz, index 63 = ~20kHz
  ],
  "timestamp": "2025-04-13T14:00:01Z"  // timeField — UTC
}
```

| Field       | Type            | Description                                   |
|-------------|-----------------|-----------------------------------------------|
| `device_id` | string          | Sensor identifier, used as the meta field     |
| `db_level`  | float           | Overall room loudness in dBFS                 |
| `bins`      | array[64 float] | FFT energy per log-spaced band in dBFS        |
| `timestamp` | datetime (UTC)  | When the reading was captured                 |

---

## TTL / Data Retention

Documents are automatically deleted after **30 days** via the `expireAfterSeconds` setting on the time-series collection. No separate TTL index is needed — this is handled natively by the collection definition.

---

## Indexes

A compound index is created on startup to speed up the most common query pattern (fetch readings for a specific device within a time window):

```python
await collection.create_index([("device_id", 1), ("timestamp", -1)])
```

---

## Query Patterns

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
      "_id": None,
      "min_db": {"$min": "$db_level"},
      "max_db": {"$max": "$db_level"},
      "avg_db": {"$avg": "$db_level"},
      "count":  {"$sum": 1},
  }}
]
```

---

## Environment Variables

```
MONGO_URI=mongodb+srv://<user>:<pass>@<cluster>.mongodb.net/<dbname>?retryWrites=true&w=majority
MONGO_DB_NAME=audio_spectrum
```
