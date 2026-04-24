#include "Sensors.h"
#include "State.h"
#include <TSIC.h>  // TSIC library for ZACWire protocol

// Define the global state instance here (only once!)
SystemState state; 

// Define the mutex for thread-safe state access
SemaphoreHandle_t stateMutex = NULL;

// Initialize the state mutex
void initStateMutex() {
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == NULL) {
        Serial.println("[STATE] ERROR: Failed to create state mutex!");
    } else {
        Serial.println("[STATE] State mutex initialized");
    }
}

// Instantiate the TSIC 306 sensor
// TSIC(SignalPin, VCCpin, SensorType)
// NO_VCC_PIN = External power used (sensor VCC connected to external 3.3V/5V)
TSIC tempSensor(TSIC_SIGNAL_PIN, NO_VCC_PIN, TSIC_30x);

// EMA filter for temperature smoothing (reduces derivative noise)
float filteredTemp = 20.0;  // Initial value
const float EMA_ALPHA = 0.6; // CleverCoffee's smoothing factor

// Safety limits
const float TEMP_MIN_VALID = 0.0;   // Minimum plausible temperature
const float TEMP_MAX_VALID = 150.0; // Maximum plausible temperature
const int MAX_CONSECUTIVE_FAILURES = 3; // Max failures before error state

void setupSensors() {
    Serial.println("[SENSORS] Initializing TSIC 306 Temperature Sensor...");
    // The TSIC library handles pin setup internally
    // The sensor will start sending data automatically every 100ms
    Serial.println("[SENSORS] TSIC 306 ready on GPIO" + String(TSIC_SIGNAL_PIN));
    Serial.println("[SENSORS] EMA filter enabled (alpha=0.6)");
    Serial.printf("[SENSORS] Valid temp range: %.1f°C to %.1f°C\n", TEMP_MIN_VALID, TEMP_MAX_VALID);
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
        if (millis() - lastErrorPrint > 1000) {  // Print error every 1 second
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