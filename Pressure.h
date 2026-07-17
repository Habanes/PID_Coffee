#ifndef PRESSURE_H
#define PRESSURE_H

// Process 3 - Pressure. Reads the transducer ADC, reverses the divider,
// maps to Bar, and publishes currentPressure + pressureVoltage.
// No fault flag (over-range trips safePressureMax on its own).
// Only called from ControlTask.cpp when HAS_PRESSURE_SENSOR (Config.h) is 1 -
// on no-sensor builds these two functions are simply never invoked.
// See ../Processes.txt (3).

void setupPressure();   // call once in ControlTask init
void readPressure();    // call once per control cycle

#endif // PRESSURE_H
