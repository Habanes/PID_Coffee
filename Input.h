#ifndef INPUT_H
#define INPUT_H

// Process 7 - Input (IDLE-only menu). Encoder ISR + button. The button cycles
// the 5 display views; the encoder edits the active preset's coffee target /
// shot time / preset selection. Locked unless machineState == IDLE - except
// in ECO, where any encoder/button edge instead just requests a wake to IDLE
// (no view cycling, no edits).
// See ../Processes.txt (7).

void setupInput();   // attach encoder ISR + button (call once in UiTask init)
void syncInput();    // call every UiTask cycle

#endif // INPUT_H
