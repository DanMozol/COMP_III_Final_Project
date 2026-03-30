
# This script acts as the receiver
# It listens for the ESP32 and pushes the data to MongoDB Atlas cluster

from fastapi import FastAPI
from pydantic import BaseModel
from motor.motor_asyncio import AsyncIOMotorClient
import datetime

app = FastAPI()

# Replace with your actual Atlas Connection String
MONGO_DETAILS = "mongodb+srv://<username>:<password>@cluster0.mongodb.net/test"
client = AsyncIOMotorClient(MONGO_DETAILS)
database = client.spectrum_analyzer
collection = database.get_collection("fft_readings")

class FFTData(BaseModel):
    device_id: str
    bins: list[float]  # Expecting 64 bins from your FFT

@app.post("/upload")
async def upload_spectrum(data: FFTData):
    document = {
        "device_id": data.device_id,
        "bins": data.bins,
        "timestamp": datetime.datetime.utcnow()
    }
    result = await collection.insert_one(document)
    return {"status": "success", "id": str(result.inserted_id)}