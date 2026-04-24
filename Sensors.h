#ifndef SENSORS_H
#define SENSORS_H
#include <Arduino.h>
#include "Config.h"

// VCC: External power used (connect sensor VCC to 3.3V or 5V)

void setupSensors();
void readTemperature();

#endif