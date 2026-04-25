// ESP32 Room Audio Monitor — Firmware
// Hardware: ESP32 + INMP441 I2S digital microphone
//
// What this does every second:
//   1. Reads 1024 samples from the INMP441 via I2S
//   2. Computes overall dBFS level from the raw samples
//   3. Runs a windowed FFT and maps the result to 64 log-spaced bands (20Hz–20kHz)
//   4. POSTs { device_id, db_level, bins[64] } to the FastAPI server with Bearer auth

#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <ArduinoJson.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Credentials injected at build time from secrets.ini (never committed)
// Copy secrets.ini.example → secrets.ini and fill in your values
// ---------------------------------------------------------------------------
const char* WIFI_SSID  = CONFIG_WIFI_SSID;
const char* WIFI_PASS  = CONFIG_WIFI_PASS;
const char* SERVER_URL = CONFIG_SERVER_URL;
const char* API_TOKEN  = CONFIG_API_TOKEN;
const char* DEVICE_ID  = "ESP32_STATION_01";

// ---------------------------------------------------------------------------
// I2S pins for INMP441
//   INMP441 → ESP32
//   VDD     → 3.3V
//   GND     → GND
//   L/R     → GND   (selects left channel)
//   WS      → GPIO 25
//   SCK     → GPIO 26
//   SD      → GPIO 22
// ---------------------------------------------------------------------------
#define I2S_WS_PIN   25
#define I2S_SCK_PIN  26
#define I2S_SD_PIN   22
#define I2S_PORT     I2S_NUM_0

// ---------------------------------------------------------------------------
// Audio / FFT config
// ---------------------------------------------------------------------------
#define SAMPLE_RATE   44100
#define FFT_SAMPLES   512    // must be a power of 2
#define NUM_BANDS     64

// Must be global — large stack allocations crash the ESP32
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
int32_t i2s_raw[FFT_SAMPLES * 2];

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

// ---------------------------------------------------------------------------
// I2S init
// ---------------------------------------------------------------------------
void i2s_init() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 128,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_SCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD_PIN,
    };

    i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_PORT, &pins);
    i2s_zero_dma_buffer(I2S_PORT);
}

// ---------------------------------------------------------------------------
// Read FFT_SAMPLES raw 32-bit samples from the INMP441 into vReal[].
// The INMP441 outputs 24-bit audio left-justified in a 32-bit word,
// so we right-shift 8 bits then normalize to the -1.0..+1.0 range.
// ---------------------------------------------------------------------------
void read_samples() {
    size_t bytes_read = 0;

    i2s_read(I2S_PORT, i2s_raw, sizeof(i2s_raw), &bytes_read, portMAX_DELAY);

    // Even indices = left channel (INMP441 with L/R tied to GND)
    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = (double)(i2s_raw[i * 2] >> 8) / 8388608.0;
        vImag[i] = 0.0;
    }
}

// ---------------------------------------------------------------------------
// Compute overall dBFS from the raw time-domain samples.
// Call this BEFORE running the FFT (FFT overwrites vReal).
// Returns -96.0 dBFS when the signal is below the noise floor.
// ---------------------------------------------------------------------------
float compute_db() {
    double sum = 0.0;
    for (int i = 0; i < FFT_SAMPLES; i++) {
        sum += vReal[i] * vReal[i];
    }
    double rms = sqrt(sum / FFT_SAMPLES);
    if (rms < 1e-10) return -96.0f;
    return (float)(20.0 * log10(rms));
}

// ---------------------------------------------------------------------------
// Map FFT magnitude bins to 64 logarithmically-spaced frequency bands.
// Must be called AFTER FFT.complexToMagnitude() so vReal holds magnitudes.
// Output bands[] are in dBFS.
// ---------------------------------------------------------------------------
void compute_bands(float bands[NUM_BANDS]) {
    const float F_MIN = 20.0f;
    const float F_MAX = 20000.0f;

    for (int b = 0; b < NUM_BANDS; b++) {
        // Log-spaced band edges
        float f_low  = F_MIN * powf(F_MAX / F_MIN, (float)b       / NUM_BANDS);
        float f_high = F_MIN * powf(F_MAX / F_MIN, (float)(b + 1) / NUM_BANDS);

        // Map to FFT bin indices
        int bin_low  = (int)(f_low  * FFT_SAMPLES / SAMPLE_RATE);
        int bin_high = (int)(f_high * FFT_SAMPLES / SAMPLE_RATE);

        bin_low  = max(bin_low,  1);
        bin_high = min(bin_high, FFT_SAMPLES / 2 - 1);
        if (bin_high < bin_low) bin_high = bin_low;

        // Average magnitude across the bins in this band
        double sum = 0.0;
        int    cnt = 0;
        for (int i = bin_low; i <= bin_high; i++) {
            sum += vReal[i];
            cnt++;
        }

        double avg = (cnt > 0) ? sum / cnt : 0.0;
        if (avg < 1e-10) avg = 1e-10;

        // Normalize against FFT_SAMPLES/2 and convert to dB
        bands[b] = (float)(20.0 * log10(avg / (FFT_SAMPLES / 2)));
    }
}

// ---------------------------------------------------------------------------
// Build JSON and POST to the FastAPI server
// ---------------------------------------------------------------------------
void post_data(float db_level, float bands[NUM_BANDS]) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Not connected — skipping POST");
        return;
    }

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["db_level"]  = round(db_level * 10.0f) / 10.0f;

    JsonArray arr = doc["bins"].to<JsonArray>();
    for (int i = 0; i < NUM_BANDS; i++) {
        arr.add(round(bands[i] * 10.0f) / 10.0f);
    }

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Authorization", String("Bearer ") + API_TOKEN);

    int code = http.POST(body);
    Serial.printf("[POST] %d  dB=%.1f\n", code, db_level);
    http.end();
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());

    // Init I2S mic
    i2s_init();
    Serial.println("[I2S] Initialized — INMP441 ready");
}

// ---------------------------------------------------------------------------
// Reconnect to WiFi if connection dropped
// ---------------------------------------------------------------------------
void wifi_reconnect() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.print("[WiFi] Reconnecting");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Reconnected: " + WiFi.localIP().toString());
    } else {
        Serial.println("\n[WiFi] Reconnect failed — will retry next cycle");
    }
}

// ---------------------------------------------------------------------------
// Main loop — runs once per second
// ---------------------------------------------------------------------------
void loop() {
    wifi_reconnect();

    // 1. Read raw mic samples
    read_samples();

    // 2. Compute dB BEFORE FFT modifies the buffer
    float db_level = compute_db();

    // 3. Apply Hamming window + forward FFT + convert to magnitudes
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    // 4. Map to 64 log-spaced bands
    float bands[NUM_BANDS];
    compute_bands(bands);

    // 5. Debug output
    Serial.printf("[Audio] dB=%.1f  Band[0]=%.1f  Band[32]=%.1f  Band[63]=%.1f\n",
                  db_level, bands[0], bands[32], bands[63]);

    // 6. POST to API
    post_data(db_level, bands);

    delay(250);
}
