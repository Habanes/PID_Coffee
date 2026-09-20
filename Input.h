#ifndef INPUT_H
#define INPUT_H

// Process 7 - Input (IDLE-only menu). Encoder ISR + button. The button cycles
// the 8 display views; the encoder edits the active preset's coffee target /
// per-phase brew timings (preinfuse/bloom/preheat/boost) / preset selection.
// Locked unless machineState == IDLE - except in ECO/SLEEP, where any
// encoder/button edge instead just requests a wake to IDLE (no view cycling,
// no edits). Long-press from the PRESET view opens a nested override/save
// screen (see Settings.h PRESET MODEL / LIVE-EDIT MODEL) rather than being a
// DisplayView of its own.
// See ../Processes.txt (7).

void setupInput();   // attach encoder ISR + button (call once in UiTask init)
void syncInput();    // call every UiTask cycle

#endif // INPUT_H
