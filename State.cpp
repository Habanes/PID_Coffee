#include <Arduino.h>
#include "State.h"

// Global state instance — shared across all modules
SystemState state;

// Mutex for thread-safe access to state
SemaphoreHandle_t stateMutex = NULL;

void initStateMutex() {
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == NULL) {
        Serial.println("[STATE] ERROR: Failed to create state mutex!");
    } else {
        Serial.println("[STATE] State mutex initialized");
    }
}
