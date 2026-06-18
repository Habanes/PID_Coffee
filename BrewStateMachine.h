#ifndef BREW_STATE_MACHINE_H
#define BREW_STATE_MACHINE_H

// Process 4 - Brewing state machine (main controller). Decides the mode and
// every output: machineState, coffeeSubstate, currentTargetTemperature,
// heaterMode, pumpState, valveState (+ their GPIOs), brewTimerElapsedMs,
// errorReason. Runs BEFORE the PID each control cycle.
// See ../Processes.txt (4) and ../Architecture.txt "STATE MACHINES".

void setupBrew();              // pin modes + init (call once in ControlTask init)
void updateBrewStateMachine(); // call once per control cycle

#endif // BREW_STATE_MACHINE_H
