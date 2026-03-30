// This is a starting version. This focuses on WiFi connection and the HTTP POST logic. 
// specific FFT math will be added soon

#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverUrl = "http://<YOUR_LAPTOP_IP>:8000/upload";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Mock data for testing later: 64 bins of 0.0
    String httpRequestData = "{\"device_id\":\"ESP32_STATION_01\", \"bins\":[0.1, 0.2, 0.3]}";
    
    int httpResponseCode = http.POST(httpRequestData);
    Serial.println("Response: " + String(httpResponseCode));
    
    http.end();
  }
  delay(5000); // Send data every 5 seconds
}