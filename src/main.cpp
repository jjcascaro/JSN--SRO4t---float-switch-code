#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>
#include "secrets.h"

// HC-SR04 Pins
#define TRIG_PIN D1  // GPIO13 - Trigger
#define ECHO_PIN D2  // GPIO15 - Echo
// Float switch pin
#define FLOAT_SWITCH_PIN D5  // GPIO14 - Float switch (D0/GPIO16 does not support INPUT_PULLUP)

const int FLOOD_SENSOR_ID = 1; // PK of the FloodSensor record in Django admin (api/flood-sensors/1/)

float distance = 0;
unsigned long lastTime = 0;

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

void sendData(float dist, bool isHigh) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected!");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  // Start HTTP connection
  if (!http.begin(client, BACKEND_URL)) {
    Serial.println("ERROR: Failed to connect to backend");
    return;
  }

  // Set header
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-API-Key", API_KEY);

  // Get timestamp
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", timeinfo);

  // Create JSON string — matches FloodSensorReadingSerializer fields
  // flood_sensor = integer PK of the FloodSensor record in Django
  // device_timestamp = optional ISO-style timestamp reported by the device
  String jsonData = "{\"flood_sensor\":" + String(FLOOD_SENSOR_ID)
                    + ",\"water_level\":" + String(dist, 1)
                    + ",\"device_timestamp\":\"" + String(timestamp) + "\"}";

  Serial.print("Sending JSON: ");
  Serial.println(jsonData);

  // Send POST request
  int httpCode = http.POST(jsonData);

  if (httpCode > 0) {
    Serial.print("HTTP Status: ");
    Serial.println(httpCode);
    String response = http.getString();
    if (response.length() > 0) {
      Serial.print("Response: ");
      Serial.println(response);
    }
    // --- Diagnostics ---
    if (httpCode == 201 || httpCode == 200) {
      Serial.println("[OK] Data saved successfully.");
    } else if (httpCode == 400) {
      Serial.println("[ERROR 400] Bad Request — Django rejected the data.");
      Serial.println("  Check: field names match serializer (flood_sensor, water_level, device_timestamp)");
      Serial.println("  Check: flood_sensor ID exists in Django admin");
      Serial.println("  Check: timestamp format is correct (YYYY-MM-DDTHH:MM:SS)");
    } else if (httpCode == 401) {
      Serial.println("[ERROR 401] Unauthorized — API key is missing or wrong.");
      Serial.println("  Check: API_KEY value matches what is stored in Django");
    } else if (httpCode == 403) {
      Serial.println("[ERROR 403] Forbidden — API key was rejected by the server.");
      Serial.println("  Check: The sensor device is registered and active in Django admin");
    } else if (httpCode == 404) {
      Serial.println("[ERROR 404] URL not found — BACKEND_URL is wrong.");
      Serial.println("  Check: BACKEND_URL path is correct");
    } else if (httpCode == 500) {
      Serial.println("[ERROR 500] Server crashed — check Django server terminal for traceback.");
    } else {
      Serial.print("[WARN] Unhandled status code: ");
      Serial.println(httpCode);
    }
  } else {
    Serial.print("HTTP Error code: ");
    Serial.println(httpCode);
    Serial.print("HTTP Error string: ");
    Serial.println(http.errorToString(httpCode));
    // --- Diagnostics ---
    if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) {
      Serial.println("[ERROR] Connection refused — server not running or wrong IP/port.");
      Serial.println("  Check: Django server is running (python manage.py runserver 0.0.0.0:8000)");
      Serial.print("  Check: PC IP is still "); Serial.println(BACKEND_URL);
      Serial.println("  Fix: Run 'ipconfig' on your PC and update BACKEND_URL if IP changed");
    } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
      Serial.println("[ERROR] Timeout — server connected but did not respond in time.");
      Serial.println("  Check: Django server is not frozen or overloaded");
    } else if (httpCode == HTTPC_ERROR_NOT_CONNECTED) {
      Serial.println("[ERROR] Not connected — WiFi dropped before sending.");
    } else {
      Serial.print("  WiFi status: ");
      Serial.println(WiFi.status());
    }
  }

  http.end();
}


float measureDistance() {
  // Send 10µs pulse to trigger
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Measure echo pulse width
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // max 30ms timeout
  
  // Calculate distance: (duration / 2) / 29.1
  // 29.1 microseconds per cm (speed of sound)
  float dist = (duration / 2.0) / 29.1;
  
  return dist;
}

void setup() {
  Serial.begin(74880);
  delay(1000);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP); // Internal pull-up: LOW = switch closed, HIGH = switch open

  connectWiFi();
}

void loop() {
  if (millis() - lastTime >= 10000) { // Measure every 10 seconds
    lastTime = millis();

    // Get current date and time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", timeinfo);
    Serial.print("[Time] ");
    Serial.println(timestamp);
    
    distance = measureDistance();
    
    Serial.print("Distance: ");
    Serial.print(distance, 1);
    Serial.println(" cm");
    
    // Float switch sensor reading (INPUT_PULLUP: LOW = closed/HIGH water, HIGH = open/LOW water)
    int sensorState = digitalRead(FLOAT_SWITCH_PIN);
    bool waterHigh = (sensorState == LOW);
    if (waterHigh) {
      Serial.println("Water level: HIGH (Switch Closed)");
    } else {
      Serial.println("Water level: LOW (Switch Open)");
    }

    // WiFi status check
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    
    // Send data to backend
    sendData(distance, waterHigh);
    Serial.println("---");
  }
}

