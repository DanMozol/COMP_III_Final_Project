from fastapi import FastAPI, Depends, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from motor.motor_asyncio import AsyncIOMotorClient
from dotenv import load_dotenv
from typing import Optional
from bson import ObjectId
import datetime
import os

load_dotenv()

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
MONGO_URI    = os.getenv("MONGO_URI", "mongodb://localhost:27017")
MONGO_DB     = os.getenv("MONGO_DB_NAME", "audio_spectrum")
API_TOKEN    = os.getenv("API_TOKEN", "changeme")

# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------
app = FastAPI(title="Room Audio Monitor API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------
client       = AsyncIOMotorClient(MONGO_URI)
db           = client[MONGO_DB]
collection   = db["readings"]
sessions_col = db["sessions"]

@app.on_event("startup")
async def startup():
    existing = await db.list_collection_names()
    if "readings" not in existing:
        await db.create_collection(
            "readings",
            timeseries={
                "timeField": "timestamp",
                "metaField": "device_id",
                "granularity": "seconds",
            },
            expireAfterSeconds=60 * 60 * 24 * 30,
        )
        print("Created time-series collection: readings")

    await collection.create_index([("device_id", 1), ("timestamp", -1)])
    await sessions_col.create_index([("start_time", -1)])

# ---------------------------------------------------------------------------
# Auth
# ---------------------------------------------------------------------------
security = HTTPBearer()

def verify_token(credentials: HTTPAuthorizationCredentials = Depends(security)):
    if credentials.credentials != API_TOKEN:
        raise HTTPException(status_code=401, detail="Invalid token")
    return credentials.credentials

# ---------------------------------------------------------------------------
# WebSocket connection manager
# ---------------------------------------------------------------------------
class ConnectionManager:
    def __init__(self):
        self.active: list[WebSocket] = []

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active.append(ws)

    def disconnect(self, ws: WebSocket):
        if ws in self.active:
            self.active.remove(ws)

    async def broadcast(self, payload: dict):
        dead = []
        for ws in self.active:
            try:
                await ws.send_json(payload)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)

manager = ConnectionManager()

# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------
class AudioReading(BaseModel):
    device_id: str
    db_level: float = Field(..., description="Overall dBFS level")
    bins: list[float] = Field(..., min_length=64, max_length=64, description="64 FFT frequency bands")

class SessionCreate(BaseModel):
    name: str

# ---------------------------------------------------------------------------
# Routes — data ingestion
# ---------------------------------------------------------------------------

@app.post("/data", summary="Ingest a reading from the ESP32")
async def post_data(reading: AudioReading, _: str = Depends(verify_token)):
    now = datetime.datetime.utcnow()
    document = {
        "device_id": reading.device_id,
        "db_level":  reading.db_level,
        "bins":      reading.bins,
        "timestamp": now,
    }
    result = await collection.insert_one(document)

    payload = {
        "device_id": reading.device_id,
        "db_level":  reading.db_level,
        "bins":      reading.bins,
        "timestamp": now.isoformat(),
    }
    await manager.broadcast(payload)

    return {"status": "ok", "id": str(result.inserted_id)}


@app.get("/history", summary="Return readings over a time window")
async def get_history(
    window:    str           = "1h",
    device_id: Optional[str] = None,
    _:         str           = Depends(verify_token),
):
    hours = {"1h": 1, "6h": 6, "24h": 24}.get(window, 1)
    since = datetime.datetime.utcnow() - datetime.timedelta(hours=hours)

    query: dict = {"timestamp": {"$gte": since}}
    if device_id:
        query["device_id"] = device_id

    cursor   = collection.find(query, {"_id": 0}).sort("timestamp", 1)
    readings = await cursor.to_list(length=None)

    for r in readings:
        r["timestamp"] = r["timestamp"].isoformat()

    return readings


@app.get("/stats", summary="Min / max / avg dB over a time window")
async def get_stats(
    window:    str           = "1h",
    device_id: Optional[str] = None,
    _:         str           = Depends(verify_token),
):
    hours = {"1h": 1, "6h": 6, "24h": 24}.get(window, 1)
    since = datetime.datetime.utcnow() - datetime.timedelta(hours=hours)

    match: dict = {"timestamp": {"$gte": since}}
    if device_id:
        match["device_id"] = device_id

    pipeline = [
        {"$match": match},
        {"$group": {
            "_id":    None,
            "min_db": {"$min": "$db_level"},
            "max_db": {"$max": "$db_level"},
            "avg_db": {"$avg": "$db_level"},
            "count":  {"$sum": 1},
        }},
    ]

    result = await collection.aggregate(pipeline).to_list(length=1)
    if not result:
        return {"min_db": None, "max_db": None, "avg_db": None, "count": 0, "window": window}

    r = result[0]
    return {
        "min_db": round(r["min_db"], 2),
        "max_db": round(r["max_db"], 2),
        "avg_db": round(r["avg_db"], 2),
        "count":  r["count"],
        "window": window,
    }

# ---------------------------------------------------------------------------
# Routes — sessions
# ---------------------------------------------------------------------------

@app.post("/sessions", summary="Start a named session")
async def start_session(session: SessionCreate, _: str = Depends(verify_token)):
    doc = {
        "name":       session.name,
        "start_time": datetime.datetime.utcnow(),
        "end_time":   None,
    }
    result = await sessions_col.insert_one(doc)
    return {"id": str(result.inserted_id), "name": session.name}


@app.put("/sessions/{session_id}/end", summary="End a session")
async def end_session(session_id: str, _: str = Depends(verify_token)):
    result = await sessions_col.update_one(
        {"_id": ObjectId(session_id)},
        {"$set": {"end_time": datetime.datetime.utcnow()}}
    )
    if result.matched_count == 0:
        raise HTTPException(status_code=404, detail="Session not found")
    return {"status": "ok"}


@app.get("/sessions", summary="List all sessions")
async def list_sessions(_: str = Depends(verify_token)):
    cursor = sessions_col.find({}).sort("start_time", -1)
    sessions = await cursor.to_list(length=100)
    for s in sessions:
        s["id"] = str(s.pop("_id"))
        s["start_time"] = s["start_time"].isoformat()
        if s["end_time"]:
            s["end_time"] = s["end_time"].isoformat()
    return sessions


@app.get("/sessions/{session_id}/average", summary="Averaged spectrum for a session")
async def session_average(session_id: str, _: str = Depends(verify_token)):
    session = await sessions_col.find_one({"_id": ObjectId(session_id)})
    if not session:
        raise HTTPException(status_code=404, detail="Session not found")

    end_time = session["end_time"] or datetime.datetime.utcnow()

    cursor = collection.find(
        {"timestamp": {"$gte": session["start_time"], "$lte": end_time}},
        {"bins": 1, "db_level": 1, "_id": 0}
    )
    readings = await cursor.to_list(length=None)

    if not readings:
        return {
            "id": session_id, "name": session["name"],
            "count": 0, "avg_db": None, "avg_bins": None,
            "start_time": session["start_time"].isoformat(),
            "end_time": end_time.isoformat(),
        }

    n          = len(readings)
    num_bands  = len(readings[0]["bins"])
    avg_bins   = [0.0] * num_bands
    avg_db     = 0.0

    for r in readings:
        avg_db += r["db_level"]
        for i, v in enumerate(r["bins"]):
            avg_bins[i] += v

    avg_db  = round(avg_db / n, 2)
    avg_bins = [round(v / n, 2) for v in avg_bins]

    return {
        "id":         session_id,
        "name":       session["name"],
        "start_time": session["start_time"].isoformat(),
        "end_time":   end_time.isoformat(),
        "count":      n,
        "avg_db":     avg_db,
        "avg_bins":   avg_bins,
    }


# ---------------------------------------------------------------------------
# WebSocket
# ---------------------------------------------------------------------------

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await manager.connect(ws)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(ws)
