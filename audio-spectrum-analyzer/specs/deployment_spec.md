# Deployment Specification
### Room Audio Monitor — Local Dev & Render.com

---

## Local Development

### Requirements

- Python 3.11 (not 3.12+ — see [Known Issues](#known-issues))
- PlatformIO (VS Code extension or CLI)
- MongoDB Atlas cluster with your local IP allowed (or `0.0.0.0/0`)

### Install Python 3.11 (macOS)

```bash
brew install python@3.11
```

### Backend Setup

```bash
cd audio-spectrum-analyzer/backend

# Create venv with Python 3.11 explicitly
/opt/homebrew/opt/python@3.11/bin/python3.11 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Create `audio-spectrum-analyzer/backend/.env`:
```
MONGO_URI=mongodb+srv://<user>:<pass>@<cluster>.mongodb.net/?retryWrites=true&w=majority
MONGO_DB_NAME=audio_spectrum
API_TOKEN=your-secret-token-here
```

### Run Everything

```bash
cd audio-spectrum-analyzer
./start.sh
```

`start.sh` automatically uses the `.venv` if present, falls back to system uvicorn. It also opens the frontend in your browser and opens a serial monitor for the ESP32 in a new Terminal window.

Or run the backend alone:
```bash
cd audio-spectrum-analyzer/backend
source .venv/bin/activate
uvicorn main:app --host 0.0.0.0 --port 8000
```

Frontend is served by FastAPI at `http://localhost:8000`.

---

## Render Deployment

### Preferred: Blueprint (automatic)

1. Push repo to GitHub
2. In Render: **New → Blueprint**, connect your GitHub repo
3. Render reads `render.yaml` from the repo root automatically
4. Add env vars in Render dashboard:
   - `MONGO_URI`
   - `MONGO_DB_NAME`
   - `API_TOKEN`
   - `PYTHON_VERSION` = `3.11.9`

### Manual Service (if not using Blueprint)

If you created the service via **New → Web Service** instead, Render ignores `render.yaml`. Configure manually under Settings:

| Setting          | Value                                    |
|------------------|------------------------------------------|
| Root Directory   | `audio-spectrum-analyzer/backend`        |
| Build Command    | `pip install -r requirements.txt`        |
| Start Command    | `uvicorn main:app --host 0.0.0.0 --port $PORT` |
| PYTHON_VERSION   | `3.11.9` (env var)                       |
| MONGO_URI        | your Atlas connection string (env var)   |
| MONGO_DB_NAME    | `audio_spectrum` (env var)               |
| API_TOKEN        | your secret token (env var)              |

### Update Firmware for Render

After deployment, update `SERVER_URL` in `firmware/src/main.cpp`:
```cpp
const char* SERVER_URL = "https://your-service.onrender.com/data";
```
Reflash all boards.

---

## Environment Variables Reference

| Variable        | Required | Description                              |
|-----------------|----------|------------------------------------------|
| `MONGO_URI`     | Yes      | MongoDB Atlas connection string          |
| `MONGO_DB_NAME` | Yes      | Database name (e.g. `audio_spectrum`)    |
| `API_TOKEN`     | Yes      | Bearer token — must match firmware       |
| `PYTHON_VERSION`| Render   | Set to `3.11.9` to pin Python on Render  |

---

## Known Issues

### Python 3.13 + MongoDB Atlas SSL failure

Python 3.13 uses OpenSSL 3.0, which is incompatible with MongoDB Atlas's TLS handshake (`TLSV1_ALERT_INTERNAL_ERROR`). This is a server-side cipher rejection — no pymongo or certifi parameter fixes it. The only solution is Python 3.11 (uses OpenSSL 1.1.x).

This affects **local development only**. Render is configured to use Python 3.11.9 via the `PYTHON_VERSION` env var and is unaffected.

### Render Free Tier Cold Starts

Render spins down free services after ~15 minutes of inactivity. The first request after idle takes ~30 seconds to cold-start. Subsequent requests are fast. Plan accordingly for demos — hit the backend URL in a browser before presenting to wake it up.

### MongoDB Atlas IP Allowlist

Atlas blocks all connections by default. For Render deployment, set Network Access to `0.0.0.0/0`. For local dev, add your current machine's IP address.
