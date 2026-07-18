#include "Pressure.h"
#include "State.h"
#include "Config.h"

#if SIMULATION_MODE

void setupPressure() {}

void readPressure() {
    STATE_LOCK();
    state.currentPressure = SIM_PRESSURE_BAR;
    state.pressureVoltage = 0.0f;   // no real ADC in simulation
    STATE_UNLOCK();
}

#else

void setupPressure() {
    analogSetPinAttenuation(PIN_PRESSURE, ADC_11db);   // full 0-3.3V range
}

void readPressure() {
    // 1. Average a few samples to tame ADC noise.
    uint32_t sum = 0;
    for (int i = 0; i < PRESSURE_ADC_SAMPLES; i++) sum += analogRead(PIN_PRESSURE);
    float vGpio = (sum / (float)PRESSURE_ADC_SAMPLES) * (3.3f / 4095.0f);

    // 2. Reverse the divider to recover the sensor output voltage.
    float vSensor = vGpio / PRESSURE_DIVIDER_RATIO;

    // 3. Linear map V->Bar (0.5V = 0 Bar, 4.5V = full scale), clamped.
    float bar = (vSensor - PRESSURE_SENSOR_V_LOW)
              / (PRESSURE_SENSOR_V_HIGH - PRESSURE_SENSOR_V_LOW)
              * PRESSURE_RANGE_BAR;
    if (bar < 0.0f)               bar = 0.0f;
    if (bar > PRESSURE_RANGE_BAR) bar = PRESSURE_RANGE_BAR;

    STATE_LOCK();
    state.currentPressure = bar;
    state.pressureVoltage = vGpio;   // raw GPIO voltage, diagnostic
    STATE_UNLOCK();
}

#endif // SIMULATION_MODE
