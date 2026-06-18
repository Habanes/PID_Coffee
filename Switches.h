#ifndef SWITCHES_H
#define SWITCHES_H

// Process 1 - Switch state machine. Reads the combined ladder ADC, decodes
// into 4 bands, debounces (rejects the steam pulse), and publishes
// switchVoltage + switchSteam + switchCoffee.
// See ../Processes.txt (1).

void setupSwitches();   // call once in ControlTask init
void readSwitches();    // call once per control cycle

#endif // SWITCHES_H
