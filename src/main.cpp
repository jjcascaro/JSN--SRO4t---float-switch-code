#include <Arduino.h>

// HC-SR04 Pins
#define TRIG_PIN D1  // GPIO13 - Trigger
#define ECHO_PIN D2  // GPIO15 - Echo
// Float switch pin
#define FLOAT_SWITCH_PIN D5  // GPIO14 - Float switch (D0/GPIO16 does not support INPUT_PULLUP)

float distance = 0;
unsigned long lastTime = 0;

void setup() {
  Serial.begin(74880);
  delay(1000);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP); // Internal pull-up: LOW = switch closed, HIGH = switch open
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

void loop() {
  if (millis() - lastTime >= 1000) { // Measure every 1 seconds
    lastTime = millis();
    
    distance = measureDistance();
    
    Serial.print("Distance: ");
    Serial.print(distance, 1);
    Serial.println(" cm");
    
    // Float switch sensor reading (INPUT_PULLUP: LOW = closed/HIGH water, HIGH = open/LOW water)
    int sensorState = digitalRead(FLOAT_SWITCH_PIN);
    if (sensorState == LOW) {
      Serial.println("Water level: HIGH (Switch Closed)");
    } else {
      Serial.println("Water level: LOW (Switch Open)");
    }
  }
}

