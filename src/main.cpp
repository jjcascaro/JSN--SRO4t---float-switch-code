#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "secrets.h"

// ===== ENVIRONMENT CONFIGURATION =====
#define USE_PRODUCTION 0  // Set to 1 for production HTTPS, 0 for local HTTP

// ===== SENSOR PINS =====
#define TRIG_PIN D1              // GPIO13 - Ultrasonic trigger
#define ECHO_PIN D2              // GPIO15 - Ultrasonic echo
#define FLOAT_SWITCH_PIN D5      // GPIO14 - Float switch (INPUT_PULLUP)

// ===== DEVICE FINGERPRINTING =====
#define DEVICE_TYPE "ESP8266-HC-SR04-FloatSwitch"
#define FIRMWARE_VERSION "1.0.0"

// ===== ISRG ROOT X1 CA CERTIFICATE (Public, safe to embed) =====
// This is the Let's Encrypt root CA used to verify sbs.techadviseph.com
// Source: https://raw.githubusercontent.com/letsencrypt/root_certificates/main/certificates/isrg-root-x1.pem
const char* ISRG_ROOT_X1 = "cert";

float distance = 0;
unsigned long lastTime = 0;

// ===== DEVICE FINGERPRINTING =====
String getDeviceMAC() {
  return WiFi.macAddress();
}

String generateRequestID() {
  // Simple request ID based on time + random
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

  // Sync time via NTP (UTC+8 for Philippines)
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
  // Format: YYYY-MM-DDTHH:MM:SS+08:00
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", timeinfo);
  return String(timestamp) + "+08:00";
}

void sendData(float waterLevel, bool floatSwitchTriggered) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected!");
    return;
  }

#if USE_PRODUCTION
  // === PRODUCTION: HTTPS with certificate validation ===
  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1);  // Validate server certificate
  
  HTTPClient http;
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println("ERROR: Failed to initialize HTTPS connection");
    return;
  }
#else
  // === LOCAL: HTTP without encryption (testing only) ===
  WiFiClient client;
  HTTPClient http;
  
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println("ERROR: Failed to initialize HTTP connection");
    return;
  }
#endif

  // ===== SET HEADERS =====
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-API-Key", API_KEY);
  http.addHeader("X-Device-Type", DEVICE_TYPE);
  http.addHeader("X-Firmware-Version", FIRMWARE_VERSION);
  http.addHeader("X-Device-MAC", getDeviceMAC());
  http.addHeader("X-Request-ID", generateRequestID());
  http.addHeader("User-Agent", "ESP8266-FloodSensor/1.0");

  // ===== BUILD JSON PAYLOAD =====
  // NOTE: Device is identified by X-Device-API-Key header, NOT by body field
  // Body contains: water_level, float_switch, device_timestamp
  String jsonData = "{\"water_level\":" + String(waterLevel, 2)
                    + ",\"float_switch\":" + String(floatSwitchTriggered ? 1 : 0)
                    + ",\"device_timestamp\":\"" + getISO8601Timestamp() + "\"}";

  Serial.print("Sending JSON: ");
  Serial.println(jsonData);

  // ===== SEND POST REQUEST =====
  int httpCode = http.POST(jsonData);

  // ===== HANDLE RESPONSE =====
  if (httpCode > 0) {
    Serial.print("HTTP Status: ");
    Serial.println(httpCode);
    String response = http.getString();
    if (response.length() > 0) {
      Serial.print("Response: ");
      Serial.println(response);
    }

    // Diagnostics
    if (httpCode == 201 || httpCode == 200) {
      Serial.println("[OK] Data queued successfully.");
    } else if (httpCode == 400) {
      Serial.println("[ERROR 400] Bad Request — field validation failed.");
      Serial.println("  Check: JSON fields (water_level, float_switch, device_timestamp)");
    } else if (httpCode == 401) {
      Serial.println("[ERROR 401] Unauthorized — API key missing or invalid.");
      Serial.println("  Check: X-Device-API-Key header value matches Django");
    } else if (httpCode == 429) {
      Serial.println("[ERROR 429] Rate Limited — too many requests.");
      Serial.println("  Wait: Limit is 60 requests/minute per IP");
      Serial.println("  Check: Device sending data too frequently");
    } else if (httpCode == 500) {
      Serial.println("[ERROR 500] Server error — check backend logs");
    } else {
      Serial.print("[WARN] Unhandled HTTP code: ");
      Serial.println(httpCode);
    }
  } else {
    Serial.print("[ERROR] HTTP Error: ");
    Serial.println(http.errorToString(httpCode));

    if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) {
      Serial.println("  → Server not running or wrong address");
    } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
      Serial.println("  → Server not responding in time");
    } else if (httpCode == HTTPC_ERROR_NOT_CONNECTED) {
      Serial.println("  → WiFi dropped");
    }
  }

  http.end();
}

float measureDistance() {
  // Send 10µs trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure echo pulse (max 30ms timeout)
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  // Distance = (duration / 2) / 29.1 cm per microsecond
  float dist = (duration / 2.0) / 29.1;

  return dist;
}

void setup() {
  Serial.begin(115200);  // Standard ESP8266 baud rate
  delay(1000);

  // Pin setup
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP);

  Serial.println("\n\n=== FLOOD SENSOR STARTING ===");
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("Device MAC: ");
  Serial.println(getDeviceMAC());
  Serial.print("Mode: ");
  Serial.println(USE_PRODUCTION ? "PRODUCTION (HTTPS)" : "LOCAL (HTTP)");
  Serial.println("==============================\n");

  connectWiFi();
}

void loop() {
  if (millis() - lastTime >= 10000) {  // Send every 10 seconds
    lastTime = millis();

    // Log timestamp
    Serial.print("[");
    Serial.print(getISO8601Timestamp());
    Serial.println("]");

    // Measure water level
    distance = measureDistance();
    Serial.print("Distance: ");
    Serial.print(distance, 1);
    Serial.println(" cm");

    // Read float switch (INPUT_PULLUP: LOW = water high, HIGH = water low)
    int sensorState = digitalRead(FLOAT_SWITCH_PIN);
    bool floatTriggered = (sensorState == LOW);
    Serial.println(floatTriggered ? "Float: TRIGGERED (water high)" : "Float: NORMAL (water low)");

    // WiFi status
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED");

    // Send data
    sendData(distance, floatTriggered);
    Serial.println("---\n");
  }
}