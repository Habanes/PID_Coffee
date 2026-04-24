#include "Controls.h"
#include "State.h"
#include <PID_v1.h>
#include <Preferences.h>

// Preferences instance for persistent storage
Preferences preferences;

// PID Variables
double pidInput = 0.0;      // Current temperature
double pidOutput = 0.0;     // PID output (0-1000ms) - for PID library
double pidSetpoint = DEFAULT_TARGET_TEMP;  // Target temperature

// Atomic 32-bit copy for ISR (double is 8 bytes and not atomically readable;
// uint32_t is atomically R/W on ESP32's 32-bit Xtensa core)
volatile uint32_t pidOutputISR = 0;

// PID Tuning Parameters - Heating Mode
static double Kp = DEFAULT_KP;
static double Ki = DEFAULT_KI;
static double Kd = DEFAULT_KD;
static double IMax = DEFAULT_IMAX;
static double emaFactor = DEFAULT_EMA_FACTOR;

// PID Tuning Parameters - Brew Mode
static double BrewKp = DEFAULT_BREW_KP;
static double BrewKi = DEFAULT_BREW_KI;
static double BrewKd = DEFAULT_BREW_KD;
static int brewDelaySeconds = DEFAULT_BREW_DELAY_SECONDS;

// Brew mode state tracking
static bool brewModeActive = false;
static bool brewPidDisabled = false;
static unsigned long brewStartTime = 0;

// PID Controller Instance (8-arg rancilio fork: arg7=pMode 1=P_ON_E, arg8=DIRECT)
PID myPID(&pidInput, &pidOutput, &pidSetpoint, DEFAULT_KP, DEFAULT_KI, DEFAULT_KD, 1, DIRECT);

// Hardware Timer for SSR Control
hw_timer_t* pidTimer = NULL;

// ISR Variables
const unsigned int windowSize = SSR_WINDOW_MS;  // Time-proportional window
volatile unsigned int isrCounter = 0;            // Counter increments by PID_TIMER_INTERVAL_MS each tick
volatile bool emergencyStopActive = false;
volatile bool relayForceOff = false;     // Force relay OFF for testing/cooling
volatile unsigned long isrTickCount = 0; // Diagnostic: total ISR executions

/**
 * @brief Hardware Timer ISR - Controls SSR via time-proportional switching
 * 
 * Runs at 100Hz (every 10ms). Compares pidOutput against isrCounter to
 * create a time-proportional output over a 1-second window.
 * 
 * Example: If pidOutput = 600, SSR is ON for 600ms, OFF for 400ms each second.
 */
void IRAM_ATTR onPIDTimer() {
    isrTickCount++; // Diagnostic counter
    
    // Emergency stop overrides everything
    if (emergencyStopActive) {
        digitalWrite(RELAY_PIN, LOW);
        isrCounter += PID_TIMER_INTERVAL_MS;
        if (isrCounter >= windowSize) {
            isrCounter = 0;
        }
        return;
    }
    
    // Force relay off for testing (allows cool-down without PID interference)
    if (relayForceOff) {
        digitalWrite(RELAY_PIN, LOW);
        isrCounter += PID_TIMER_INTERVAL_MS;
        if (isrCounter >= windowSize) {
            isrCounter = 0;
        }
        return;
    }
    
    // Time-proportional control (read atomic 32-bit copy, not the 8-byte double)
    if (isrCounter >= pidOutputISR) {
        digitalWrite(RELAY_PIN, LOW);  // Turn SSR OFF
    } else {
        digitalWrite(RELAY_PIN, HIGH); // Turn SSR ON
    }
    
    isrCounter += PID_TIMER_INTERVAL_MS;
    
    // Reset counter every SSR_WINDOW_MS
    if (isrCounter >= windowSize) {
        isrCounter = 0;
    }
}

void setupControls() {
    // Setup Relay Pin
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    
    // Configure PID Controller
    pidSetpoint = state.setTemp;  // Already loaded from storage
    myPID.SetSampleTime(windowSize);
    myPID.SetOutputLimits(0, windowSize);
    myPID.SetIntegratorLimits(0, IMax);
    myPID.SetSmoothingFactor(emaFactor);
    myPID.SetMode(AUTOMATIC);
    myPID.SetTunings(Kp, Ki, Kd, 1);
    
    // Setup Hardware Timer (1MHz base, 100Hz alarm)
    pidTimer = timerBegin(1000000);
    if (pidTimer == NULL) {
        Serial.println("[CONTROLS] ERROR: Failed to create timer!");
        return;
    }
    timerAttachInterrupt(pidTimer, &onPIDTimer);
    timerAlarm(pidTimer, PID_TIMER_INTERVAL_MS * 1000UL, true, 0);
    
    // Verify timer is ticking
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (isrTickCount > 0) {
        Serial.printf("[CONTROLS] Ready | Kp=%.1f Ki=%.3f Kd=%.1f | Setpoint=%.1f°C\n", Kp, Ki, Kd, pidSetpoint);
    } else {
        Serial.println("[CONTROLS] WARNING: Timer ISR not executing!");
    }
}

void updatePID() {
    // Read state with mutex protection
    STATE_LOCK();
    bool sensorErr = state.sensorError;
    float currentTemp = state.currentTemp;
    float setTemp = state.setTemp;
    bool brewMode = state.brewMode;
    STATE_UNLOCK();
    
    // CRITICAL SAFETY CHECK #1: Sensor Error
    if (sensorErr) {
        if (!emergencyStopActive) {
            emergencyStop();
            Serial.println("[CONTROLS] !!! EMERGENCY STOP: Sensor error detected !!!");
        }
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        return;
    }
    
    // CRITICAL SAFETY CHECK #2: Temperature out of plausible range
    if (currentTemp < TEMP_MIN_VALID || currentTemp > EMERGENCY_STOP_TEMP) {
        if (!emergencyStopActive) {
            emergencyStop();
            Serial.printf("[CONTROLS] !!! EMERGENCY STOP: Implausible temperature %.1f\u00b0C !!!\n", currentTemp);
        }
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        return;
    }
    
    // Clear emergency stop if temperature is back to safe range
    if (emergencyStopActive && currentTemp < (setTemp + EMERGENCY_STOP_HYSTERESIS)) {
        emergencyStopActive = false;
        Serial.println("[CONTROLS] Emergency stop cleared - temperature safe");
    }

    // --- BREW MODE TRANSITION LOGIC (matching CleverCoffee) ---
    if (brewMode && !brewModeActive) {
        // Brew mode just activated: disable PID for initial delay (no boost, just off)
        brewModeActive = true;
        brewPidDisabled = true;
        brewStartTime = millis();
        myPID.SetMode(MANUAL);
        pidOutput = 0;
        digitalWrite(RELAY_PIN, LOW);
        Serial.printf("[CONTROLS] Brew mode ON - PID disabled for %ds delay\n", brewDelaySeconds);
    } else if (!brewMode && brewModeActive) {
        // Brew mode deactivated: restore normal heating PID
        brewModeActive = false;
        brewPidDisabled = false;
        myPID.SetMode(AUTOMATIC);
        myPID.SetTunings(Kp, Ki, Kd, 1);
        Serial.println("[CONTROLS] Brew mode OFF - back to heating PID");
    }

    // After brew delay: enable brew PID tunings
    if (brewModeActive && brewPidDisabled) {
        if ((millis() - brewStartTime) >= (unsigned long)brewDelaySeconds * 1000UL) {
            brewPidDisabled = false;
            myPID.SetMode(AUTOMATIC);
            myPID.SetTunings(BrewKp, BrewKi, BrewKd, 1);
            Serial.println("[CONTROLS] Brew delay elapsed - brew PID active");
        }
    }

    // PID off during brew delay: force output to zero
    if (brewPidDisabled) {
        pidOutput = 0;
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        return;
    }

    // --- PID COMPUTE ---
    pidInput = currentTemp;
    pidSetpoint = setTemp;

    if (!brewModeActive) {
        // Normal mode: re-apply heating tunings (allows live tuning updates)
        myPID.SetTunings(Kp, Ki, Kd, 1);
    }

    myPID.Compute();
    pidOutputISR = (uint32_t)pidOutput;  // Atomic 32-bit copy for ISR
    
    // Store output in global state for display/debugging (thread-safe)
    STATE_LOCK();
    state.pidOutput = pidOutput;
    STATE_UNLOCK();
    
    // Console output every 1 second
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > CONTROLS_DEBUG_MS) {
        float dutyCycle = (pidOutput / windowSize * 100.0);
        const char* modeStr = brewModeActive ? "BREW" : "HEAT";
        Serial.printf("[%s] %.1f°C → %.1f°C | %.1f%% duty\n",
                      modeStr, setTemp, currentTemp, dutyCycle);
        lastDebug = millis();
    }
}

void resetPIDMemory() {
    // Zero the integral accumulator by reinitialising PID with output forced to 0.
    // PID_v1 sets outputSum = pidOutput on SetMode(AUTOMATIC), so we zero pidOutput first.
    pidOutput = 0.0;
    myPID.SetMode(MANUAL);
    myPID.SetMode(AUTOMATIC);
}

void emergencyStop() {
    emergencyStopActive = true;
    pidOutput = 0;
    digitalWrite(RELAY_PIN, LOW);

    STATE_LOCK();
    float currentTemp = state.currentTemp;
    STATE_UNLOCK();

    Serial.printf("\n!!! EMERGENCY STOP: %.1f°C (limit %.1f°C) !!!\n", currentTemp, (float)EMERGENCY_STOP_TEMP);
}

void setRelayForceOff(bool forceOff) {
    relayForceOff = forceOff;
    if (forceOff) {
        Serial.println("[CONTROLS] Relay force-off ENABLED - heater suspended for testing");
    } else {
        Serial.println("[CONTROLS] Relay force-off DISABLED - resuming PID control");
    }
}

bool isRelayForceOff() {
    return relayForceOff;
}

// ========== BREW MODE FUNCTIONS ==========

void setBrewMode(bool active) {
    STATE_LOCK();
    state.brewMode = active;
    STATE_UNLOCK();
}

bool isBrewModeActive() {
    STATE_LOCK();
    bool active = state.brewMode;
    STATE_UNLOCK();
    return active;
}

bool isBrewBoostPhase() {
    return brewModeActive && brewPidDisabled;
}

void setBrewPIDTunings(double kp, double ki, double kd, int delaySeconds) {
    BrewKp = kp;
    BrewKi = ki;
    BrewKd = kd;
    brewDelaySeconds = delaySeconds;
    // Apply immediately if brew PID is already active (delay has elapsed)
    if (brewModeActive && !brewPidDisabled) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd, 1);
    }
    saveBrewSettingsToStorage();
    Serial.printf("[CONTROLS] Brew PID updated: Kp=%.1f, Ki=%.3f, Kd=%.1f, Delay=%ds\n",
                  BrewKp, BrewKi, BrewKd, brewDelaySeconds);
}

void getBrewPIDTunings(double &kp, double &ki, double &kd, int &delaySeconds) {
    kp = BrewKp;
    ki = BrewKi;
    kd = BrewKd;
    delaySeconds = brewDelaySeconds;
}

void resetBrewPIDToDefaults() {
    BrewKp = DEFAULT_BREW_KP;
    BrewKi = DEFAULT_BREW_KI;
    BrewKd = DEFAULT_BREW_KD;
    brewDelaySeconds = DEFAULT_BREW_DELAY_SECONDS;
    if (brewModeActive && !brewPidDisabled) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd, 1);
    }
    saveBrewSettingsToStorage();
    Serial.println("[CONTROLS] Brew PID reset to factory defaults");
}

void saveBrewSettingsToStorage() {
    preferences.begin("coffee-pid", false);
    preferences.putDouble("brewKp", BrewKp);
    preferences.putDouble("brewKi", BrewKi);
    preferences.putDouble("brewKd", BrewKd);
    preferences.putInt("brewDelay", brewDelaySeconds);
    preferences.end();
}

// ========== HEATING PID API FUNCTIONS ==========

void setPIDTunings(double kp, double ki, double kd) {
    Kp = kp;
    Ki = ki;
    Kd = kd;
    myPID.SetTunings(Kp, Ki, Kd, 1);
    savePIDToStorage();
    Serial.printf("[CONTROLS] PID tunings updated: Kp=%.1f, Ki=%.3f, Kd=%.1f (saved to NVS)\n", Kp, Ki, Kd);
}

void getPIDTunings(double &kp, double &ki, double &kd) {
    kp = Kp;
    ki = Ki;
    kd = Kd;
}

void setTargetTemp(double temp) {
    if (temp >= SETTEMP_MIN && temp <= SETTEMP_MAX) {
        STATE_LOCK();
        state.setTemp = temp;
        STATE_UNLOCK();
        pidSetpoint = temp;
        savePIDToStorage();
    }
}

bool isEmergencyStopActive() {
    return emergencyStopActive;
}

// ========== PERSISTENT STORAGE FUNCTIONS ==========

/**
 * @brief Load PID parameters from ESP32 NVS (Non-Volatile Storage)
 * 
 * Uses Preferences library to retrieve stored values.
 * If no values are stored, defaults from Controls.h are used.
 */
void loadPIDFromStorage() {
    preferences.begin("coffee-pid", true); // Read-only mode
    
    // Load PID tunings (use defaults if not found)
    Kp = preferences.getDouble("Kp", DEFAULT_KP);
    Ki = preferences.getDouble("Ki", DEFAULT_KI);
    Kd = preferences.getDouble("Kd", DEFAULT_KD);
    IMax = preferences.getDouble("IMax", DEFAULT_IMAX);
    emaFactor = preferences.getDouble("emaFactor", DEFAULT_EMA_FACTOR);
    
    // Load target temperature
    state.setTemp = preferences.getDouble("targetTemp", DEFAULT_TARGET_TEMP);
    pidSetpoint = state.setTemp;

    // Load brew mode settings
    BrewKp = preferences.getDouble("brewKp", DEFAULT_BREW_KP);
    BrewKi = preferences.getDouble("brewKi", DEFAULT_BREW_KI);
    BrewKd = preferences.getDouble("brewKd", DEFAULT_BREW_KD);
    brewDelaySeconds = preferences.getInt("brewDelay", DEFAULT_BREW_DELAY_SECONDS);
    
    preferences.end();
    Serial.printf("[STORAGE] Loaded | Kp=%.1f Ki=%.3f Kd=%.1f | Target=%.1f°C | Brew delay=%ds\n",
                  Kp, Ki, Kd, state.setTemp, brewDelaySeconds);
}

/**
 * @brief Save current PID parameters to ESP32 NVS
 * 
 * Stores values persistently - they survive reboots and power loss.
 */
void savePIDToStorage() {
    preferences.begin("coffee-pid", false); // Read-write mode
    
    preferences.putDouble("Kp", Kp);
    preferences.putDouble("Ki", Ki);
    preferences.putDouble("Kd", Kd);
    preferences.putDouble("IMax", IMax);
    preferences.putDouble("emaFactor", emaFactor);
    preferences.putDouble("targetTemp", state.setTemp);
    
    preferences.end();
}

/**
 * @brief Reset PID parameters to factory defaults
 * 
 * Restores default values from Controls.h and saves them to storage.
 */
void resetPIDToDefaults() {
    Kp = DEFAULT_KP;
    Ki = DEFAULT_KI;
    Kd = DEFAULT_KD;
    IMax = DEFAULT_IMAX;
    emaFactor = DEFAULT_EMA_FACTOR;

    STATE_LOCK();
    state.setTemp = DEFAULT_TARGET_TEMP;
    STATE_UNLOCK();
    pidSetpoint = DEFAULT_TARGET_TEMP;

    myPID.SetTunings(Kp, Ki, Kd, 1);
    myPID.SetIntegratorLimits(0, IMax);
    myPID.SetSmoothingFactor(emaFactor);

    savePIDToStorage();
    Serial.printf("[STORAGE] Reset to defaults | Kp=%.1f Ki=%.3f Kd=%.1f | Target=%.1f°C\n",
                  Kp, Ki, Kd, state.setTemp);
}