#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <time.h>
#include "secrets.h"

// ===== ENVIRONMENT CONFIGURATION =====
#define USE_PRODUCTION 1  // Set to 1 for HTTPS, 0 for HTTP (local testing)

// ===== SENSOR PINS =====
#define TRIG_PIN D1              // GPIO5  - Ultrasonic trigger
#define ECHO_PIN D2              // GPIO4  - Ultrasonic echo
#define FLOAT_SWITCH_PIN D5      // GPIO14 - Float switch

// ===== DEVICE FINGERPRINTING =====
#define DEVICE_TYPE "ESP8266-JSN-SRO4T-FloatSwitch"
#define FIRMWARE_VERSION "1.0.0"

float distance = 0;
unsigned long lastTime = 0;

String getDeviceMAC() {
  return WiFi.macAddress();
}

String generateRequestID() {
  return String(millis()) + "-" + String(random(1000, 9999));
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Sync time via NTP (UTC+8 for Philippines = 28800 seconds offset)
  configTime(28800, 0, "pool.ntp.org");
  Serial.print("Syncing time");
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synced!");
}

String getISO8601Timestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", timeinfo);
  return String(timestamp) + "+08:00";
}

void sendData(float waterLevel, bool floatSwitchTriggered) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected!");
    return;
  }

#if USE_PRODUCTION
  BearSSL::X509List cert(ISRG_E7_CERT);
  WiFiClientSecure client;
  client.setTrustAnchors(&cert);
  HTTPClient http;
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println("ERROR: Failed to initialize HTTPS connection");
    return;
  }
#else
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println("ERROR: Failed to initialize HTTP connection");
    return;
  }
#endif

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-API-Key", API_KEY);
  http.addHeader("X-Device-Type", DEVICE_TYPE);
  http.addHeader("X-Firmware-Version", FIRMWARE_VERSION);
  http.addHeader("X-Device-MAC", getDeviceMAC());
  http.addHeader("X-Request-ID", generateRequestID());
  http.addHeader("User-Agent", "ESP8266-FloodSensor/1.0");

  String jsonData = "{\"water_level\":" + String(waterLevel, 2)
                    + ",\"float_switch\":" + String(floatSwitchTriggered ? 1 : 0)
                    + ",\"device_timestamp\":\"" + getISO8601Timestamp() + "\"}";

  Serial.print("Sending JSON: ");
  Serial.println(jsonData);

  int httpCode = http.POST(jsonData);

  if (httpCode > 0) {
    Serial.print("HTTP Status: ");
    Serial.println(httpCode);
    String response = http.getString();
    if (response.length() > 0) {
      Serial.print("Response: ");
      Serial.println(response);
    }

    if (httpCode == 201 || httpCode == 200) {
      Serial.println("[OK] Data queued successfully.");
    } else if (httpCode == 401) {
      Serial.println("[ERROR 401] Unauthorized — API key invalid.");
    } else if (httpCode == 429) {
      Serial.println("[ERROR 429] Rate Limited — too many requests.");
    }
  } else {
    Serial.print("[ERROR] HTTP Error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}


float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float dist = (duration / 2.0) / 29.1;

  return dist;
}

void setup() {
  Serial.begin(74880);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP);

  Serial.println("\n\n=== FLOOD SENSOR STARTING ===");
  Serial.println("Firmware: " FIRMWARE_VERSION);
  Serial.print("Mode: ");
  Serial.println(USE_PRODUCTION ? "PRODUCTION (HTTPS)" : "LOCAL (HTTP)");
  Serial.println("==============================\n");

  connectWiFi();
  randomSeed(micros());  // Seed RNG after WiFi/NTP delays for better entropy
}

void loop() {
  if (millis() - lastTime >= 10000) {
    lastTime = millis();

    Serial.print("[");
    Serial.print(getISO8601Timestamp());
    Serial.println("]");

    distance = measureDistance();
    Serial.print("Distance: ");
    Serial.print(distance, 1);
    Serial.println(" cm");

    // INPUT_PULLUP: LOW = switch closed (water high), HIGH = switch open (water low)
    int sensorState = digitalRead(FLOAT_SWITCH_PIN);
    bool floatTriggered = (sensorState == LOW);
    Serial.println(floatTriggered ? "Float: TRIGGERED" : "Float: NORMAL");

    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED");

    sendData(distance, floatTriggered);
    Serial.println("---\n");
  }
}

