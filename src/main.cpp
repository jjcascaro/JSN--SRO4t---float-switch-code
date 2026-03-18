#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// HC-SR04 Pins
#define TRIG_PIN D1  // GPIO13 - Trigger
#define ECHO_PIN D2  // GPIO15 - Echo
// Float switch pin
#define FLOAT_SWITCH_PIN D5  // GPIO14 - Float switch (D0/GPIO16 does not support INPUT_PULLUP)

// ---- WiFi Config ----
const char* WIFI_SSID     = "JjA15";
const char* WIFI_PASSWORD = "112075oratej";
const char* BACKEND_URL   = "http://your-backend.com/api/sensor"; // Change this to your backend
// ---------------------

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
}

void sendData(float dist, bool isHigh) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected!");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  http.begin(client, BACKEND_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"distance\":" + String(dist, 1) +
                   ",\"water_level\":\"" + (isHigh ? "HIGH" : "LOW") + "\"}");

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.print("HTTP Response: ");
    Serial.println(httpCode);
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("Data sent successfully!");
    }
  } else {
    Serial.print("HTTP failed: ");
    Serial.println(http.errorToString(httpCode));
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
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  }
}

