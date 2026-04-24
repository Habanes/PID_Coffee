#include "Sensors.h"
#include "State.h"
#include <TSIC.h>  // TSIC library for ZACWire protocol

// Instantiate the TSIC 306 sensor
// TSIC(SignalPin, VCCpin, SensorType)
// NO_VCC_PIN = External power used (sensor VCC connected to external 3.3V/5V)
TSIC tempSensor(TSIC_SIGNAL_PIN, NO_VCC_PIN, TSIC_30x);

// Last valid filtered temperature (EMA-smoothed); seeded at room temperature
float filteredTemp = SENSOR_EMA_SEED_TEMP;

void setupSensors() {
    Serial.printf("[SENSORS] TSIC 306 ready (GPIO %d, EMA=%.1f, range %.0f-%.0f°C)\n",
                  TSIC_SIGNAL_PIN, (float)EMA_ALPHA, (float)TEMP_MIN_VALID, (float)TEMP_MAX_VALID);
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