# Firmware Specification
### Room Audio Monitor — ESP32 + INMP441

---

## Hardware

| Component  | Purpose                         |
|------------|---------------------------------|
| ESP32      | Microcontroller + WiFi          |
| INMP441    | I2S digital MEMS microphone     |

### INMP441 Wiring

```
INMP441 Pin → ESP32 Pin
VDD         → 3.3V
GND         → GND
L/R         → GND    (selects left channel)
WS          → GPIO 25
SCK         → GPIO 26
SD          → GPIO 22
```

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
I2S read (1024 x 32-bit samples @ 44100 Hz)
    ↓
dBFS calculation (RMS of raw samples → 20·log10)
    ↓
Hamming window applied to samples
    ↓
Forward FFT (1024-point complex)
    ↓
Complex → magnitude
    ↓
64 log-spaced bands mapped (20Hz–20kHz)
    ↓
HTTP POST { device_id, db_level, bins[64] }
```

---

## I2S Configuration

| Parameter          | Value                       |
|--------------------|-----------------------------|
| Mode               | Master RX                   |
| Sample rate        | 44100 Hz                    |
| Bits per sample    | 32-bit (24-bit data, left-justified) |
| Channel format     | Left channel only           |
| DMA buffer count   | 8                           |
| DMA buffer length  | 128 samples                 |

---

## FFT Configuration

| Parameter     | Value                                         |
|---------------|-----------------------------------------------|
| FFT size      | 1024 samples                                  |
| Window        | Hamming                                       |
| Frequency res.| 44100 / 1024 ≈ 43 Hz per raw bin             |
| Output bands  | 64 logarithmically-spaced (20Hz–20kHz)        |

**Sample normalization:**
The INMP441 outputs 24-bit audio left-justified in a 32-bit I2S word.
`sample = (raw >> 8) / 8388608.0` normalizes to the -1.0..+1.0 range.

**Band mapping:**
Each of the 64 output bands covers a log-spaced frequency range.
The boundaries are: `f = 20 * (20000/20)^(band/64)` Hz.
The average FFT magnitude within each band is converted to dBFS.

---

## dB Calculation

```
RMS = sqrt( mean( sample^2 ) )   // over all 1024 raw samples
dBFS = 20 * log10( RMS )
floor = -96 dBFS                  // returned when RMS < 1e-10
```

---

## HTTP POST

- **URL:** `http://<server>:8000/data`
- **Method:** POST
- **Headers:**
  - `Content-Type: application/json`
  - `Authorization: Bearer <API_TOKEN>`
- **Body:**
```json
{
  "device_id": "ESP32_STATION_01",
  "db_level": -42.3,
  "bins": [-60.1, -58.4, ...]
}
```
- **Interval:** 1 second (`delay(1000)` at end of loop)

---

## Serial Debug Output

Every loop iteration prints:
```
[Audio] dB=-42.3  Band[0]=-60.1  Band[32]=-48.2  Band[63]=-55.9
[POST] 200  dB=-42.3
```

---

## Configuration Constants

All user-configurable values are at the top of `main.cpp`:

```cpp
const char* WIFI_SSID  = "YOUR_WIFI_SSID";
const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL = "http://<YOUR_SERVER_IP>:8000/data";
const char* API_TOKEN  = "your-secret-token-here";
const char* DEVICE_ID  = "ESP32_STATION_01";
```
