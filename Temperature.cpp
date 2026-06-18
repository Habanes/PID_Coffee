#include "Temperature.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
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

    // 2. Calibration offset (sensor reads hotter than the water at the puck).
    SETTINGS_LOCK();
    float offset = settings.tempOffset;
    SETTINGS_UNLOCK();

    // 3. Fault if no valid reading for too long.
    bool fault = (millis() - lastValidMs) > TEMP_ERROR_INTERVAL_MS;

    // 4. Publish the TRUE temperature.
    STATE_LOCK();
    state.currentTemperature     = filtered - offset;
    state.temperatureSensorError = fault;
    STATE_UNLOCK();
}
