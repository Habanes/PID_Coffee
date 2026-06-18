#ifndef TEMPERATURE_H
#define TEMPERATURE_H

// Process 2 - Temperature. Reads the TSIC, subtracts the calibration offset,
// EMA-filters, and publishes currentTemperature + temperatureSensorError.
// See ../Processes.txt (2).

void setupTemperature();    // call once in ControlTask init
void readTemperature();     // call once per control cycle

#endif // TEMPERATURE_H
