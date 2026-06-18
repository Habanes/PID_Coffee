#ifndef UI_TASK_H
#define UI_TASK_H

// Core 0 UI loop @ ~2 ms: 7-seg multiplex refresh + input/menu + buzzer tick.
// See ../FreeRTOS Management.txt.

void startUiTask();   // create the task (call once at boot)

#endif // UI_TASK_H
