#ifndef TEMPERATURE_H
#define TEMPERATURE_H

// Process 2 - Temperature. Reads the TSIC, EMA-filters, and publishes
// currentTemperature + temperatureSensorError.
// Under SIMULATION_MODE (Config.h) this is replaced by a simple integrated
// differential model instead of a real sensor read - see Temperature.cpp.
// See ../Processes.txt (2).

void setupTemperature();    // call once in ControlTask init
void readTemperature();     // call once per control cycle

#endif // TEMPERATURE_H
