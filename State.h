#ifndef STATE_H
#define STATE_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum DisplayMode {
    MODE_CURRENT,
    MODE_SET,
    MODE_DEBUG
};

struct SystemState {
    float currentTemp = 88.88;
    float setTemp = 20.0; // Initial value, will be overwritten by loadPIDFromStorage()
    float pidOutput = 0.0;
    DisplayMode displayMode = MODE_CURRENT; // Default to current temp mode
    float tempSensitivity = 0.1; // Temperature adjustment step: 0.1 or 1.0 degrees
    int consecutiveSensorFailures = 0; // Track consecutive failed sensor reads
    bool sensorError = false; // Sensor error state
    bool brewMode = false; // Brew mode active (toggled by GPIO 0 button)
};

extern SystemState state; // Tell the world this variable exists
extern SemaphoreHandle_t stateMutex; // Mutex for thread-safe state access

// Mutex helper macros for clean code
#define STATE_LOCK()   xSemaphoreTake(stateMutex, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(stateMutex)

// Initialize the state mutex (call once in setup before tasks start)
void initStateMutex();

#endif