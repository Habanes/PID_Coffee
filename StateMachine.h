#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

// Initialize outputs to safe state. Call once in setup() after setupControls().
void setupStateMachine();

// Evaluate all transitions and apply hardware outputs. Call every control loop cycle.
void updateStateMachine();

#endif
