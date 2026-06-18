#ifndef PRESSURE_H
#define PRESSURE_H

// Process 3 - Pressure. Reads the transducer ADC, reverses the divider,
// maps to Bar, and publishes currentPressure + pressureVoltage.
// No fault flag (over-range trips safePressureMax on its own).
// See ../Processes.txt (3).

void setupPressure();   // call once in ControlTask init
void readPressure();    // call once per control cycle

#endif // PRESSURE_H
