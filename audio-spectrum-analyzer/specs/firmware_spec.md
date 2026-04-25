# Firmware Specification
### Room Audio Monitor — ESP32 + INMP441

---

## Hardware

| Component  | Purpose                         |
|------------|---------------------------------|
| ESP32      | Microcontroller + WiFi          |
| INMP441    | I2S digital MEMS microphone     |

See `hardware_spec.md` for wiring.

---

## Setup & Flashing

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 connected via USB

### 1. Configure

Edit the constants at the top of `firmware/src/main.cpp`:

```cpp
const char* WIFI_SSID  = "Your_Network_Name";
const char* WIFI_PASS  = "Your_WiFi_Password";
const char* SERVER_URL = "https://your-backend.onrender.com/data";
                      // or "http://<local-ip>:8000/data" for local dev
const char* API_TOKEN  = "your-secret-token-here";   // must match backend API_TOKEN
const char* DEVICE_ID  = "ESP32_STATION_01";          // unique per board
```

For multiple boards, flash each with a unique `DEVICE_ID` (`ESP32_STATION_02`, etc). Everything else is identical.

### 2. Flash

```bash
cd audio-spectrum-analyzer/firmware
pio run --target upload
```

### 3. Monitor serial output

```bash
pio device monitor
```

Expected output once connected:
```
[WiFi] Connected: 192.168.x.x
[I2S] Initialized — INMP441 ready
[Audio] dB=-42.3  Band[0]=-60.1  Band[32]=-48.2  Band[63]=-55.9
[POST] 200  dB=-42.3
```

`[POST] 200` confirms the backend is receiving data.

---

## Libraries (PlatformIO)

| Library         | Version   | Purpose                                 |
|-----------------|-----------|-----------------------------------------|
| `arduinoFFT`    | ^2.0.2    | Fast Fourier Transform                  |
| `ArduinoJson`   | ^7.0.0    | JSON serialization for HTTP POST body   |
| `WiFi.h`        | built-in  | WiFi connection                         |
| `HTTPClient.h`  | built-in  | HTTP POST to FastAPI                    |
| `driver/i2s.h`  | built-in  | ESP-IDF I2S peripheral driver           |

---

## Audio Pipeline (runs every second)

```
I2S read (512 x 32-bit stereo samples @ 44100 Hz)
    ↓
Extract left channel only (even indices of stereo buffer)
    ↓
dBFS calculation (RMS of raw samples → 20·log10)
    ↓
Hamming window applied to samples
    ↓
Forward FFT (512-point complex)
    ↓
Complex → magnitude
    ↓
64 log-spaced bands mapped (20Hz–20kHz)
    ↓
HTTP POST { device_id, db_level, bins[64] }
```

---

## I2S Configuration

| Parameter          | Value                                       |
|--------------------|---------------------------------------------|
| Mode               | Master RX                                   |
| Sample rate        | 44100 Hz                                    |
| Bits per sample    | 32-bit (24-bit audio, left-justified)       |
| Channel format     | Right/Left stereo (left channel used)       |
| DMA buffer count   | 8                                           |
| DMA buffer length  | 128 samples                                 |

---

## FFT Configuration

| Parameter      | Value                                        |
|----------------|----------------------------------------------|
| FFT size       | 512 samples                                  |
| Window         | Hamming                                      |
| Frequency res. | 44100 / 512 ≈ 86 Hz per raw bin             |
| Output bands   | 64 logarithmically-spaced (20Hz–20kHz)       |

**Sample normalization:**
The INMP441 outputs 24-bit audio left-justified in a 32-bit I2S word.
`sample = (raw >> 8) / 8388608.0` normalizes to the -1.0..+1.0 range.

**Band mapping:**
Each of the 64 output bands covers a log-spaced frequency range.
Band edges: `f = 20 * (20000/20)^(band/64)` Hz.
The average FFT magnitude within each band is converted to dBFS.

> Note: `vReal`, `vImag`, and `i2s_raw` are declared globally to avoid stack overflow — large arrays on the ESP32 stack will crash the device.

---

## dB Calculation

```
RMS  = sqrt( mean( sample² ) )   // over all 512 raw samples
dBFS = 20 * log10( RMS )
floor = -96 dBFS                  // returned when RMS < 1e-10
```

---

## HTTP POST

- **URL:** configured via `SERVER_URL` constant
- **Method:** POST
- **Headers:**
  - `Content-Type: application/json`
  - `Authorization: Bearer <API_TOKEN>`
- **Body:**
```json
{
  "device_id": "ESP32_STATION_01",
  "db_level": -42.3,
  "bins": [-60.1, -58.4, "...64 values total..."]
}
```
- **Interval:** 1 POST per second (`delay(1000)` at end of loop)
- **On WiFi disconnect:** POST is skipped, loop continues, reconnection is automatic
