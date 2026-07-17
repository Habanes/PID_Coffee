#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

// Core 1 real-time control loop @ 10 Hz, in order:
// switch -> temperature -> pressure (optional, HAS_PRESSURE_SENSOR) ->
// brew SM -> PID -> publish to heater.
// See ../FreeRTOS Management.txt.

void startControlTask();   // create the task (call once at boot)

#endif // CONTROL_TASK_H
