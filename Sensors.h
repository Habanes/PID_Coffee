#ifndef SENSORS_H
#define SENSORS_H
#include <Arduino.h>
#include "Config.h"

// VCC: External power used (connect sensor VCC to 3.3V or 5V)

void setupSensors();
void readTemperature();
void readPressure(); // ADC on PIN_PRESSURE — averages PRESSURE_ADC_SAMPLES readings
bool isSensorTimedOut(); // true if no valid reading received for SENSOR_TIMEOUT_MS

#endif