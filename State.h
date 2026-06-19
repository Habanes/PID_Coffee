#ifndef STATE_H
#define STATE_H

// =====================================================================
// Live system state - the runtime variables, each with ONE writer.
// Guarded by stateMutex; take it for any read/write (or use stateSnapshot).
// See ../Architecture.txt "LIVE VARIABLES".
// =====================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum MachineState   { STATE_IDLE, STATE_COFFEE, STATE_STEAM, STATE_ERROR };

enum CoffeeSubstate { SUB_NONE, SUB_PREINFUSE, SUB_BLOOM, SUB_PREHEAT,
                      SUB_BREW_MAX, SUB_BREW_PID, SUB_DONE };

enum HeaterMode     { HEATER_OFF, HEATER_FULL_ON, HEATER_PID };

enum DisplayView    { VIEW_TEMP, VIEW_SET_COFFEE, VIEW_TIMER,
                      VIEW_PRESET, VIEW_IP };
#define DISPLAY_VIEW_COUNT 5

enum ErrorReason    { ERR_NONE, ERR_BOTH_SWITCHES, ERR_OVER_TEMP,
                      ERR_OVER_PRESSURE, ERR_TEMP_SENSOR };

struct SystemState {
    // --- Sensors (writer: temperature / pressure processes) ---
    float currentTemperature;       // true temp (offset already subtracted)
    float currentPressure;          // Bar
    float switchVoltage;            // diagnostic
    float pressureVoltage;          // diagnostic
    bool  temperatureSensorError;

    // --- Switches (writer: switch state machine) ---
    bool  switchSteam;
    bool  switchCoffee;

    // --- Control (writer: brew SM, except heatingPidOutput = PID) ---
    float       currentTargetTemperature;
    double      heatingPidOutput;
    HeaterMode  heaterMode;

    // --- Machine (writer: brew SM) ---
    MachineState   machineState;
    CoffeeSubstate coffeeSubstate;
    bool           pumpState;
    bool           valveState;
    uint32_t       brewTimerElapsedMs;
    ErrorReason    errorReason;

    // --- UI (writer: input process) ---
    DisplayView    displayView;
    bool           setEditDecimals;   // SET_COFFEE: edit tenths (true) vs whole degrees (false)
};

extern SystemState state;
extern SemaphoreHandle_t stateMutex;

void initState();   // create the mutex + seed defaults (call once, first)

#define STATE_LOCK()   xSemaphoreTake(stateMutex, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(stateMutex)

// Consistent copy of the whole struct under lock (for readers).
SystemState stateSnapshot();

// Human-readable text (for display / web).
const char* machineStateText(MachineState s);
const char* coffeeSubstateText(CoffeeSubstate s);
const char* errorReasonText(ErrorReason r);

#endif // STATE_H
