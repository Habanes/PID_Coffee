#include "Sensors.h"
#include "State.h"
#include <TSIC.h>  // TSIC library for ZACWire protocol
#include <Arduino.h>

// Instantiate the TSIC 306 sensor
// TSIC(SignalPin, VCCpin, SensorType)
// NO_VCC_PIN = External power used (sensor VCC connected to external 3.3V/5V)
TSIC tempSensor(TSIC_SIGNAL_PIN, NO_VCC_PIN, TSIC_30x);

// Last valid filtered temperature (EMA-smoothed); seeded at room temperature
float filteredTemp = SENSOR_EMA_SEED_TEMP;

// Timestamp of the last valid temperature reading; used for sensor timeout detection
static unsigned long lastValidReadingMillis = 0;

bool isSensorTimedOut() {
    return (millis() - lastValidReadingMillis) > SENSOR_TIMEOUT_MS;
}

void setupSensors() {
    lastValidReadingMillis = millis(); // Arm the timer from startup, not from epoch 0
    // ADC_11db attenuation gives full 0–3.3V input range on this pin
    analogSetPinAttenuation(PIN_PRESSURE, ADC_11db);
    Serial.printf("[SENSORS] TSIC 306 ready (GPIO %d, EMA=%.1f, range %.0f-%.0f°C)\n",
                  TSIC_SIGNAL_PIN, (float)EMA_ALPHA, (float)TEMP_MIN_VALID, (float)TEMP_MAX_VALID);
    Serial.printf("[SENSORS] Pressure input on GPIO %d (divider R1=2.2k R2=5.1k, range=%.0f Bar)\n",
                  PIN_PRESSURE, (float)PRESSURE_RANGE_BAR);
}

void readTemperature() {
    uint16_t rawTemp = 0;
    
    // Try to read temperature from sensor
    if (tempSensor.getTemperature(&rawTemp)) {
        // Convert raw value to Celsius (0.1°C resolution)
        float tempCelsius = tempSensor.calc_Celsius(&rawTemp);
        
        // SANITY CHECK: Validate temperature is within plausible range
        if (tempCelsius < TEMP_MIN_VALID || tempCelsius > TEMP_MAX_VALID) {
            // Invalid reading - treat as sensor failure
            STATE_LOCK();
            state.consecutiveSensorFailures++;
            int failures = state.consecutiveSensorFailures;
            
            // Check if we've exceeded failure threshold
            if (failures >= MAX_CONSECUTIVE_FAILURES) {
                state.sensorError = true;
            }
            STATE_UNLOCK();
            
            Serial.printf("[SENSORS] ERROR: Invalid temperature %.1f°C (outside %.1f-%.1f°C range)\n",
                         tempCelsius, TEMP_MIN_VALID, TEMP_MAX_VALID);
            
            if (failures >= MAX_CONSECUTIVE_FAILURES) {
                Serial.println("[SENSORS] CRITICAL: Sensor error state activated after consecutive failures!");
            }
            
            return; // Don't update temperature, keep last valid value
        }
        
        // Valid reading - apply EMA filter to reduce noise
        // This is CleverCoffee's technique to smooth derivative calculations
        filteredTemp = (EMA_ALPHA * tempCelsius) + ((1.0 - EMA_ALPHA) * filteredTemp);
        
        // Reset the sensor timeout timer — we have a good reading
        lastValidReadingMillis = millis();

        // Update global state with filtered value (thread-safe)
        STATE_LOCK();
        int prevFailures = state.consecutiveSensorFailures;
        bool wasSensorError = state.sensorError;
        
        state.currentTemp = filteredTemp;
        state.consecutiveSensorFailures = 0;
        state.sensorError = false;
        STATE_UNLOCK();
        
        // Log recovery outside mutex
        if (prevFailures > 0) {
            Serial.printf("[SENSORS] Sensor recovered after %d failures\n", prevFailures);
        }
        if (wasSensorError) {
            Serial.println("[SENSORS] Sensor error state cleared - readings valid again");
        }
        
    } else {
        // Read failed - sensor communication error
        STATE_LOCK();
        state.consecutiveSensorFailures++;
        int failures = state.consecutiveSensorFailures;
        bool wasError = state.sensorError;
        
        // Check if we've exceeded failure threshold
        if (failures >= MAX_CONSECUTIVE_FAILURES && !wasError) {
            state.sensorError = true;
        }
        STATE_UNLOCK();
        
        static unsigned long lastErrorPrint = 0;
        if (millis() - lastErrorPrint > SENSOR_ERROR_LOG_MS) {
            Serial.printf("[SENSORS] Failed to read TSIC sensor (failure %d/%d)\n",
                         failures, MAX_CONSECUTIVE_FAILURES);
            lastErrorPrint = millis();
        }
        
        if (failures >= MAX_CONSECUTIVE_FAILURES && !wasError) {
            Serial.println("[SENSORS] CRITICAL: Sensor error state activated after consecutive read failures!");
        }
        
        // Don't update state.currentTemp, keep last valid filtered value
    }
}

void readPressure() {
    // Average multiple samples to reduce ESP32 ADC noise
    int32_t sum = 0;
    for (int i = 0; i < PRESSURE_ADC_SAMPLES; i++) {
        sum += analogRead(PIN_PRESSURE);
    }
    float vGpio = (sum / (float)PRESSURE_ADC_SAMPLES) * (3.3f / 4095.0f);

    // Reverse voltage divider (R1=2.2kΩ, R2=5.1kΩ) to recover sensor output voltage
    float vSensor = vGpio / PRESSURE_DIVIDER_RATIO;

    // Linear map: PRESSURE_SENSOR_V_LOW = 0 Bar, PRESSURE_SENSOR_V_HIGH = PRESSURE_RANGE_BAR
    float bar = (vSensor - PRESSURE_SENSOR_V_LOW)
                / (PRESSURE_SENSOR_V_HIGH - PRESSURE_SENSOR_V_LOW)
                * PRESSURE_RANGE_BAR;

    // Clamp to valid range (handles ADC noise near the rails)
    if (bar < 0.0f) bar = 0.0f;
    if (bar > PRESSURE_RANGE_BAR) bar = PRESSURE_RANGE_BAR;

    STATE_LOCK();
    state.currentPressure   = bar;
    state.pressureVoltage   = vGpio;  // Raw GPIO voltage before divider reversal
    STATE_UNLOCK();
}