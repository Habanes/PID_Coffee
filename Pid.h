#ifndef PID_H
#define PID_H

// Process 5 - PID. Controls the block to currentTargetTemperature using the
// heating or brew gain set. Computes/accumulates ONLY in HEATER_PID mode and
// resets the integral on entry to PID mode (anti-windup). No offset here -
// it is already baked into currentTemperature. No safety logic - the brew SM
// owns that. See ../Processes.txt (5).

void setupPid();    // call once in ControlTask init
void computePid();  // call once per control cycle, AFTER the brew SM

#endif // PID_H
