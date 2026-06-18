#ifndef HEATER_H
#define HEATER_H

// Process 6 - Heater output. A 100 Hz hardware-timer ISR drives the SSR
// time-proportionally over SSR_WINDOW_MS. heaterUpdate() is called once per
// control cycle to publish the values the ISR uses (ISR takes no locks).
// See ../Processes.txt (6).

#include "State.h"   // HeaterMode

void setupHeater();  // pin + hardware timer (call once in ControlTask init)

// Publish the latest control decision to the ISR. cutoffTemp = steamTempMax;
// the ISR forces the heater off whenever currentTemp exceeds it.
void heaterUpdate(HeaterMode mode, uint32_t dutyMs, float currentTemp, float cutoffTemp);

#endif // HEATER_H
