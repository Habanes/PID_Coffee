#ifndef SENSORS_H
#define SENSORS_H
#include <Arduino.h>

// Pin definitions for TSIC 306 sensor
#define TSIC_SIGNAL_PIN 21  // GPIO21 - Signal pin (ZACWire)
// VCC: External power used (connect sensor VCC to 3.3V or 5V)

void setupSensors();
void readTemperature();

#endif