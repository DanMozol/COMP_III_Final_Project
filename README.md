# Room Audio Monitor
### COMP III Final Project — Dan Mozol

---

## Project Proposal

### What is this?

A real-time room audio monitoring system that uses an ESP32 microcontroller and a digital microphone to continuously measure sound levels in a room. The data is sent over WiFi to a cloud-hosted API, stored in a database, and displayed on a live web dashboard featuring an audio spectrum analyzer.

---

### What data will be collected and why?

The sensor will capture two types of audio data every second:

**1. dB Level (volume)**
The overall loudness of the room measured in decibels (dBFS — decibels relative to full scale). This tells you how loud the room is at any given moment and over time. Use cases include noise monitoring, identifying loud events, and tracking average room noise levels throughout the day.

**2. FFT Frequency Spectrum (64 bands)**
A Fast Fourier Transform breaks the audio signal into 64 frequency bands spanning 20 Hz to 20,000 Hz — the full range of human hearing. This tells you *what kinds* of sounds are present, not just how loud things are. (20–200 Hz) capture bass and rumble. (200 Hz–4 kHz) capture mids (speech and music). (4–20 kHz) capture treble. This data powers the visual EQ display on the dashboard.

Together these two data streams give a complete picture of the acoustic environment in real time.

---

### Hardware

| Component | Purpose | Estimated Cost |
|-----------|---------|---------------|
| ESP32 development board | Microcontroller + WiFi | ~$8 |
| INMP441 I2S digital microphone | Audio capture | ~$3 |
| USB cable + power supply | Power | ~$0 

**Total hardware cost: ~$11**

The INMP441 is a digital I2S microphone — it connects directly to the ESP32 with 4 wires and outputs clean digital audio without the noise problems of analog microphones. 

---

### What will the end user see on the dashboard?

**Live RTA Spectrum Analyzer**
A real-time display of all 64 frequency bands rendered as vertical bars — identical in style to professional audio analysis software. Bars are color-coded and update continuously as sound changes in the room. 

**dB Level Meter**
A  vertical bar meter showing the current overall volume of the room

**Historical dB Chart**
A chart showing how room volume has changed over hours or days, pulled from the database.

**Statistics Panel**
Min, max, and average dB levels over time windows


---

### System Architecture

```
ESP32 + INMP441 mic
    ↓ computes dB + 64-band FFT on-device
    ↓ HTTP POST every second (Bearer token auth)
FastAPI backend (Python)
    ↓ validates + stores readings
MongoDB (time-series, auto-expires after 30 days)
    ├── WebSocket → live dashboard updates
    └── REST API → historical queries + stats
React frontend
    ├── Live RTA spectrum analyzer
    ├── dB level meter
    └── Historical chart + stats
```

All backend services run in Docker and are deployed to the cloud

---

### Basic Goals
- ESP32 reads mic and POSTs dB level to API every second
- FastAPI stores readings in MongoDB with token authentication
- Dashboard displays live dB meter and scrolling history chart
- Docker deployment on cloud infrastructure

### Stretch Goals
- Full 64-band FFT spectrum analyzer on the dashboard
- WebSocket push for ~100ms live updates 
- Peak hold and decay controls on the spectrum display
- Average frequency curve overlay


