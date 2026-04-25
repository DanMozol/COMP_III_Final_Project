#!/bin/bash
ROOT="$(cd "$(dirname "$0")" && pwd)"

# Start backend in background (use .venv if present, otherwise system uvicorn)
cd "$ROOT/backend"
if [ -f ".venv/bin/uvicorn" ]; then
  UVICORN=".venv/bin/uvicorn"
else
  UVICORN="uvicorn"
fi
$UVICORN main:app --host 0.0.0.0 --port 8000 &
BACKEND_PID=$!

# Open frontend in browser
open "$ROOT/frontend/index.html"

# Open serial monitor in a new Terminal window
osascript -e "tell app \"Terminal\" to do script \"cd '$ROOT/firmware' && pio device monitor\""

echo "Backend running (PID $BACKEND_PID). Press Ctrl+C to stop."
wait $BACKEND_PID
