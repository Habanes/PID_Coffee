#ifndef SWITCHES_H
#define SWITCHES_H

// Process 1 - Switch state machine. Reads the combined ladder ADC, decodes
// into 4 bands, debounces (rejects the steam pulse), and publishes
// switchVoltage + switchSteam + switchCoffee.
// Under SIMULATION_MODE (Config.h) these are no-ops - Web.cpp's /api/sim
// handler (fake switch buttons in the GUI) becomes the sole writer of
// switchSteam/switchCoffee instead. See Switches.cpp.
// See ../Processes.txt (1).

void setupSwitches();   // call once in ControlTask init
void readSwitches();    // call once per control cycle

#endif // SWITCHES_H
