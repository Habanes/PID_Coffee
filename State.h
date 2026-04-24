#ifndef STATE_H
#define STATE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

enum DisplayMode {
    MODE_CURRENT,
    MODE_SET,
    MODE_DEBUG
};

struct SystemState {
    float currentTemp = 88.88;          // Sentinel: all segments lit until first valid read
    float setTemp = DEFAULT_TARGET_TEMP; // Overwritten by loadPIDFromStorage() on boot
    float pidOutput = 0.0;
    DisplayMode displayMode = MODE_CURRENT;
    float tempSensitivity = SENSITIVITY_FINE;
    int consecutiveSensorFailures = 0;
    bool sensorError = false;
    bool brewMode = false;
};

extern SystemState state; // Tell the world this variable exists
extern SemaphoreHandle_t stateMutex; // Mutex for thread-safe state access

// Mutex helper macros for clean code
#define STATE_LOCK()   xSemaphoreTake(stateMutex, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(stateMutex)

// Initialize the state mutex (call once in setup before tasks start)
void initStateMutex();

#endif