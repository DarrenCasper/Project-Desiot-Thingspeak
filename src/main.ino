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

// HC-SR04 Ultrasonic Sensor pins  >>> PUT YOUR PINS HERE <<<
// Avoid GPIO 4 (touch), 0/3/45/46 (strapping) and 19/20 (USB).
// IMPORTANT: ECHO outputs 5V. Use a voltage divider (e.g. 1k + 2k)
// so the ESP32-S3 pin only sees ~3.3V.
const int TRIG_PIN = 17;   // change to your TRIG pin
const int ECHO_PIN = 18;   // change to your ECHO pin

// Max wait for echo: 30 ms ≈ 5 m round trip (beyond HC-SR04's ~4 m range)
const unsigned long ECHO_TIMEOUT_US = 30000;

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

// Returns distance in cm, or -1.0 if no echo (out of range / wiring issue)
float readDistanceCm() {
  // Make sure TRIG starts LOW
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  // Send a 10 µs HIGH pulse to start a measurement
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure how long ECHO stays HIGH (round-trip time of the sound wave)
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return -1.0;
  }

  // Speed of sound ≈ 0.0343 cm/µs, divide by 2 for one-way distance
  return (duration * 0.0343f) / 2.0f;
}

// Takes 3 readings and averages the valid ones to reduce noise
float readDistanceAveraged() {
  float total = 0;
  int valid = 0;
  for (int i = 0; i < 3; i++) {
    float d = readDistanceCm();
    if (d > 0) {
      total += d;
      valid++;
    }
    delay(60); // HC-SR04 needs ~60 ms between pings to avoid echo overlap
  }
  return (valid > 0) ? (total / valid) : -1.0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32-S3 Internal Temp, Touch & HC-SR04 Sensor ---");

  // HC-SR04 pin setup
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

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
  float internalTemp = temperatureRead();

  // 2. Read Capacitive Touch Value
  uint32_t touchVal = touchRead(TOUCH_PIN);
  bool isTouched = (touchVal > touchThreshold);

  // 3. Read HC-SR04 Distance (cm)
  float distanceCm = readDistanceAveraged();

  // Print live status to Serial Monitor
  if (distanceCm > 0) {
    Serial.printf("[Live] Temp: %.2f °C | Touch Raw: %u | Hand Detected: %s | Distance: %.1f cm\n",
                  internalTemp, touchVal, isTouched ? "YES" : "NO", distanceCm);
  } else {
    Serial.printf("[Live] Temp: %.2f °C | Touch Raw: %u | Hand Detected: %s | Distance: OUT OF RANGE\n",
                  internalTemp, touchVal, isTouched ? "YES" : "NO");
  }

  // 4. Upload to ThingSpeak every 20 seconds
  if (millis() - lastUploadTime >= uploadInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;

      // Construct URL with all fields
      String url = String(thingSpeakUrl) +
                   "?api_key=" + thingSpeakApiKey +
                   "&field1=" + String(internalTemp, 2) +
                   "&field2=" + String(touchVal) +
                   "&field3=" + String(isTouched ? 1 : 0);

      // Only send distance if the reading is valid (skips bad points on the graph)
      if (distanceCm > 0) {
        url += "&field4=" + String(distanceCm, 1);
      }

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