// ESP32 Room Audio Monitor — Firmware
#include <WiFi.h>
#include <WiFiClientSecure.h>   // ← ADDED
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <ArduinoJson.h>
#include <math.h>

const char* WIFI_SSID  = CONFIG_WIFI_SSID;
const char* WIFI_PASS  = CONFIG_WIFI_PASS;
const char* SERVER_URL = CONFIG_SERVER_URL;
const char* API_TOKEN  = CONFIG_API_TOKEN;
const char* DEVICE_ID  = "ESP32_STATION_01";

static unsigned long last_post_ms = 0;
#define POST_INTERVAL_MS 1000  // 1 Hz — Render free tier gets overwhelmed at 4 Hz TLS

#define I2S_WS_PIN   25
#define I2S_SCK_PIN  26
#define I2S_SD_PIN   22
#define I2S_PORT     I2S_NUM_0

#define SAMPLE_RATE   44100
#define FFT_SAMPLES   512
#define NUM_BANDS     64

#define MIC_SENSITIVITY    -26      // dBFS @ 94 dB SPL (INMP441 datasheet)
#define MIC_REF_DB          94.0    // SPL reference point (1 Pa)
#define MIC_OFFSET_DB       3.0103  // 20*log10(sqrt(2)) — peak-to-RMS correction
#define MIC_BITS            24      // valid bits from INMP441
#define MIC_OVERLOAD_DB    116.0    // INMP441 acoustic overload point
#define MIC_NOISE_DB        29.0    // INMP441 noise floor

// Reference amplitude in normalized domain (samples = raw_24bit / 2^24)
constexpr double MIC_REF_AMPL = pow(10.0, (double)MIC_SENSITIVITY / 20.0) *
                                 ((1 << (MIC_BITS - 1)) - 1) / (double)(1 << MIC_BITS);

#define SMOOTH_ALPHA  0.3f

static float smoothed_spl = 0.0f;  // seeded on first real reading

double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
int32_t i2s_raw[FFT_SAMPLES * 2];

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

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

void read_samples() {
    size_t bytes_read = 0;
    i2s_read(I2S_PORT, i2s_raw, sizeof(i2s_raw), &bytes_read, portMAX_DELAY);

    for (int i = 0; i < FFT_SAMPLES; i++) {
        // FIXED: INMP441 is 24-bit left-justified in 32-bit word
        // Correct shift is >> 11 (not >> 8), normalize to 24-bit range
        vReal[i] = (double)(i2s_raw[i * 2] >> 11) / 2097152.0;  // 2^21
        vImag[i] = 0.0;
    }
}

float compute_db(float &spl_out) {
    double sum = 0.0;
    for (int i = 0; i < FFT_SAMPLES; i++) {
        sum += vReal[i] * vReal[i];
    }
    double rms = sqrt(sum / FFT_SAMPLES);
    if (rms < 1e-10) {
        spl_out = smoothed_spl;
        return -96.0f;
    }

    float dbfs = (float)(20.0 * log10(rms));
    float raw_spl = (float)(MIC_OFFSET_DB + MIC_REF_DB + 20.0 * log10(rms / MIC_REF_AMPL));
    raw_spl = constrain(raw_spl, (float)MIC_NOISE_DB, (float)MIC_OVERLOAD_DB);

    static bool initialized = false;
    static int  skip_count  = 0;

    if (!initialized) {
        skip_count++;
        if (skip_count >= 2) {      // discard first sample (mic transient), seed on second
            smoothed_spl = raw_spl;
            initialized  = true;
        }
        spl_out = raw_spl;
        return dbfs;
    }

    smoothed_spl = SMOOTH_ALPHA * raw_spl + (1.0f - SMOOTH_ALPHA) * smoothed_spl;
    spl_out = smoothed_spl;
    return dbfs;
}

void compute_bands(float bands[NUM_BANDS]) {
    const float F_MIN = 20.0f;
    const float F_MAX = 20000.0f;

    for (int b = 0; b < NUM_BANDS; b++) {
        float f_low  = F_MIN * powf(F_MAX / F_MIN, (float)b       / NUM_BANDS);
        float f_high = F_MIN * powf(F_MAX / F_MIN, (float)(b + 1) / NUM_BANDS);

        int bin_low  = (int)(f_low  * FFT_SAMPLES / SAMPLE_RATE);
        int bin_high = (int)(f_high * FFT_SAMPLES / SAMPLE_RATE);

        bin_low  = max(bin_low,  1);
        bin_high = min(bin_high, FFT_SAMPLES / 2 - 1);
        if (bin_high < bin_low) bin_high = bin_low;

        double sum = 0.0;
        int    cnt = 0;
        for (int i = bin_low; i <= bin_high; i++) {
            sum += vReal[i];
            cnt++;
        }

        double avg = (cnt > 0) ? sum / cnt : 0.0;
        if (avg < 1e-10) avg = 1e-10;

        bands[b] = (float)(20.0 * log10(avg / FFT_SAMPLES));
    }
}

void post_data(float db_level, float spl, float bands[NUM_BANDS]) {
    unsigned long now = millis();
    if (now - last_post_ms < POST_INTERVAL_MS) return;
    last_post_ms = now;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Not connected — skipping POST");
        return;
    }

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["db_level"]  = round(db_level * 10.0f) / 10.0f;
    doc["spl"]       = round(spl * 10.0f) / 10.0f;

    JsonArray arr = doc["bins"].to<JsonArray>();
    for (int i = 0; i < NUM_BANDS; i++) {
        arr.add(round(bands[i] * 10.0f) / 10.0f);
    }

    String body;
    serializeJson(doc, body);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(12);  // TCP connect timeout in seconds for WiFiClientSecure

    HTTPClient http;
    http.begin(client, SERVER_URL);
    http.setTimeout(10000);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Authorization", String("Bearer ") + API_TOKEN);

    int code = http.POST(body);
    Serial.printf("[POST] %d  dB=%.1f dBFS  SPL=%.1f dB\n", code, db_level, spl);

    http.end();
    client.stop();
    delay(100);  // let mbedTLS fully release the socket before next iteration
}

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());

    i2s_init();
    Serial.println("[I2S] Initialized — INMP441 ready");
}

void wifi_reconnect() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("[WiFi] Lost connection — full reset");
    WiFi.disconnect(true);  // clears AP record from driver, releases stale TLS socket lock
    delay(1000);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Reconnected: " + WiFi.localIP().toString());
        delay(3000);  // DNS resolver needs time after DHCP — logs show DNS failures without this
    } else {
        Serial.println("\n[WiFi] Still disconnected — will retry");
        WiFi.disconnect(true);
    }
}

void loop() {
    wifi_reconnect();

    read_samples();

    float spl = 0.0f;
    float db_level = compute_db(spl);

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    float bands[NUM_BANDS];
    compute_bands(bands);

    Serial.printf("[Audio] dB=%.1f dBFS  SPL=%.1f dB  Band[0]=%.1f  Band[32]=%.1f  Band[63]=%.1f\n",
                  db_level, spl, bands[0], bands[32], bands[63]);

    post_data(db_level, spl, bands);

}