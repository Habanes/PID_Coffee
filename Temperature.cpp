#include "Temperature.h"
#include "State.h"
#include "Config.h"

#if SIMULATION_MODE
// ---------------------------------------------------------------------
// Simulated block: simple differential model, integrated once per control
// cycle. Reads the PREVIOUS cycle's heaterMode/heatingPidOutput/pumpState
// (Temperature runs before the brew SM/PID each cycle) - one 100ms-stale
// read is irrelevant for a toy model.
// ---------------------------------------------------------------------
static float simTemp = SIM_START_TEMP;

void setupTemperature() {
    simTemp = SIM_START_TEMP;
}

void readTemperature() {
    STATE_LOCK();
    HeaterMode hm     = state.heaterMode;
    double     duty   = state.heatingPidOutput;
    bool       pumpOn = state.pumpState;
    STATE_UNLOCK();

    float heaterOnFraction = 0.0f;
    if (hm == HEATER_FULL_ON) heaterOnFraction = 1.0f;
    else if (hm == HEATER_PID) heaterOnFraction = (float)(duty / (double)SSR_WINDOW_MS);

    float dTdt = heaterOnFraction * SIM_HEATER_GAIN_C_PER_S
               - SIM_STATIC_LOSS_C_PER_S
               - (pumpOn ? SIM_PUMP_LOSS_C_PER_S : 0.0f);
    simTemp += dTdt * (TASK_CONTROL_CYCLE_MS / 1000.0f);
    if (simTemp < SIM_AMBIENT_TEMP) simTemp = SIM_AMBIENT_TEMP;

    STATE_LOCK();
    state.currentTemperature     = simTemp;
    state.temperatureSensorError = false;
    STATE_UNLOCK();
}

#else
#include <TSIC.h>

// External VCC -> NO_VCC_PIN; TSIC 30x family on the signal pin.
static TSIC     sensor(PIN_TSIC, NO_VCC_PIN, TSIC_30x);
static float    filtered    = EMA_SEED;     // EMA of raw sensor reading
static uint32_t lastValidMs = 0;            // last in-range reading time

void setupTemperature() {
    filtered    = EMA_SEED;
    lastValidMs = millis();
}

void readTemperature() {
    uint16_t raw;

    // 1. Read + validate; EMA only on good readings.
    if (sensor.getTemperature(&raw)) {
        float c = sensor.calc_Celsius(&raw);
        if (c >= TEMP_MIN_VALID && c <= TEMP_MAX_VALID) {
            filtered    = EMA_ALPHA * c + (1.0f - EMA_ALPHA) * filtered;
            lastValidMs = millis();
        }
    }

    // 2. Fault if no valid reading for too long.
    bool fault = (millis() - lastValidMs) > TEMP_ERROR_INTERVAL_MS;

    // 3. Publish the temperature.
    STATE_LOCK();
    state.currentTemperature     = filtered;
    state.temperatureSensorError = fault;
    STATE_UNLOCK();
}

#endif // SIMULATION_MODE
