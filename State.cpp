#include "State.h"
#include "Config.h"

SystemState      state;
SemaphoreHandle_t stateMutex = NULL;

void initState() {
    stateMutex = xSemaphoreCreateMutex();

    state.currentTemperature       = EMA_SEED;
    state.currentPressure          = 0.0f;
    state.switchVoltage            = 0.0f;
    state.pressureVoltage          = 0.0f;
    state.temperatureSensorError   = false;

    state.switchSteam              = false;
    state.switchCoffee             = false;

    state.currentTargetTemperature = DEFAULT_COFFEE_TARGET_TEMP;
    state.heatingPidOutput         = 0.0;
    state.heaterMode               = HEATER_OFF;

    state.machineState             = STATE_IDLE;
    state.coffeeSubstate           = SUB_NONE;
    state.pumpState                = false;
    state.valveState               = false;
    state.brewTimerElapsedMs       = 0;
    state.errorReason              = ERR_NONE;

    state.displayView              = VIEW_TEMP;
}

SystemState stateSnapshot() {
    STATE_LOCK();
    SystemState copy = state;
    STATE_UNLOCK();
    return copy;
}

const char* machineStateText(MachineState s) {
    switch (s) {
        case STATE_IDLE:   return "Idle";
        case STATE_COFFEE: return "Coffee";
        case STATE_STEAM:  return "Steam";
        case STATE_ERROR:  return "Error";
        default:           return "?";
    }
}

const char* coffeeSubstateText(CoffeeSubstate s) {
    switch (s) {
        case SUB_PREINFUSE: return "Pre-infuse";
        case SUB_BLOOM:     return "Bloom";
        case SUB_PREHEAT:   return "Preheat";
        case SUB_BREW_MAX:  return "Brew (boost)";
        case SUB_BREW_PID:  return "Brewing";
        case SUB_DONE:      return "Done";
        default:            return "";
    }
}

const char* errorReasonText(ErrorReason r) {
    switch (r) {
        case ERR_BOTH_SWITCHES: return "Both switches active";
        case ERR_OVER_TEMP:     return "Over-temperature";
        case ERR_OVER_PRESSURE: return "Over-pressure";
        case ERR_TEMP_SENSOR:   return "Temp sensor fault";
        default:                return "";
    }
}
