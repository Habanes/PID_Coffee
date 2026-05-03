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

enum MachineState {
    STATE_IDLE,
    STATE_COFFEE,
    STATE_STEAM,
    STATE_ERROR
};

enum CoffeeSubstate {
    SUBSTATE_NONE,
    COFFEE_PREINFUSE,
    COFFEE_BLOOM,
    COFFEE_PREHEAT,
    COFFEE_BREW_MAX,
    COFFEE_BREW_PID,
    COFFEE_DONE
};

struct SystemState {
    float currentTemp = 88.88;          // Sentinel: all segments lit until first valid read
    float setTemp = DEFAULT_TARGET_TEMP; // Overwritten by loadPIDFromStorage() on boot
    float pidOutput = 0.0;
    DisplayMode displayMode = MODE_CURRENT;
    float tempSensitivity = SENSITIVITY_FINE;
    int consecutiveSensorFailures = 0;
    bool sensorError = false;
    // State machine
    MachineState machineState = STATE_IDLE;
    CoffeeSubstate coffeeSubstate = SUBSTATE_NONE;
    float currentPressure = 0.0f;
    bool swSteam = false;
    bool swCoffee = false;
    char errorReason[64] = "";   // Human-readable reason for the current ERROR state
    // Diagnostics — raw sensor voltages and output states
    float switchVoltage = 0.0f;     // Voltage at PIN_SWITCHES after resistor ladder (V)
    float pressureVoltage = 0.0f;   // Voltage at PIN_PRESSURE GPIO (V, before divider reversal)
    bool pumpOn = false;
    bool valveOn = false;
};

extern SystemState state; // Tell the world this variable exists
extern SemaphoreHandle_t stateMutex; // Mutex for thread-safe state access

// Mutex helper macros for clean code
#define STATE_LOCK()   xSemaphoreTake(stateMutex, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(stateMutex)

// Initialize the state mutex (call once in setup before tasks start)
void initStateMutex();

#endif