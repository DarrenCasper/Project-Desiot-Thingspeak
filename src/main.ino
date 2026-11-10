#include <WiFi.h>
#include <HTTPClient.h>

// --- 1. Network & ThingSpeak Credentials ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* thingSpeakApiKey = "thingspeak_API_KEY";
const char* thingSpeakUrl = "http://api.thingspeak.com/update";

// --- 2. Pin & Sensor Definitions ---
// On ESP32-S3, Touch pins are GPIO 1 through GPIO 14
const int TOUCH_PIN = 4; 

uint32_t touchBaseline = 0;
uint32_t touchThreshold = 0;

// Timing variables (ThingSpeak free tier requires >= 15s between updates)
unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 20000; // 20 seconds

void calibrateTouchSensor() {
  Serial.println("Calibrating touch wire... DO NOT TOUCH the jumper wire.");
  uint64_t sum = 0;
  const int samples = 50;
  
  for (int i = 0; i < samples; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(20);
  }
  
  touchBaseline = sum / samples;
  // On ESP32-S3, touch values INCREASE when touched.
  // Trigger when reading rises 25% above untouched baseline.
  touchThreshold = touchBaseline + (touchBaseline * 0.25); 
  
  Serial.printf("Calibration complete! Baseline: %u | Trigger Threshold: %u\n", 
                touchBaseline, touchThreshold);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32-S3 Internal Temp & Touch Sensor ---");

  // Calibrate jumper wire capacitance before Wi-Fi starts
  calibrateTouchSensor();

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected! IP Address: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  // 1. Read Internal Temperature (in Celsius)
  // Built-in Arduino-ESP32 function for ESP32-S3 internal temperature sensor
  float internalTemp = temperatureRead();

  // 2. Read Capacitive Touch Value
  uint32_t touchVal = touchRead(TOUCH_PIN);
  bool isTouched = (touchVal > touchThreshold);

  // Print live status to Serial Monitor every second for easy debugging
  Serial.printf("[Live] Temp: %.2f °C | Touch Raw: %u | Hand Detected: %s\n", 
                internalTemp, touchVal, isTouched ? "YES" : "NO");

  // 3. Upload to ThingSpeak every 20 seconds
  if (millis() - lastUploadTime >= uploadInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      
      // Construct URL with all 3 fields
      String url = String(thingSpeakUrl) + 
                   "?api_key=" + thingSpeakApiKey + 
                   "&field1=" + String(internalTemp, 2) + 
                   "&field2=" + String(touchVal) + 
                   "&field3=" + String(isTouched ? 1 : 0);

      http.begin(url);
      int httpResponseCode = http.GET();

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf(">> ThingSpeak Upload Success! (HTTP %d, Entry ID: %s)\n", 
                      httpResponseCode, response.c_str());
      } else {
        Serial.printf(">> ThingSpeak Upload Failed! Error: %s\n", 
                      http.errorToString(httpResponseCode).c_str());
      }
      http.end();
    } else {
      Serial.println(">> Wi-Fi Disconnected. Reconnecting...");
      WiFi.reconnect();
    }
    lastUploadTime = millis();
  }

  delay(1000); // 1-second local polling loop
}