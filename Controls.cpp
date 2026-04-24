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

// Volatile copy for ISR (PID library doesn't support volatile pointers)
volatile double pidOutputISR = 0.0;

// PID Tuning Parameters - Heating Mode (loaded from storage or defaults)
static double Kp = DEFAULT_KP;
static double Ki = DEFAULT_KI;
static double Kd = DEFAULT_KD;

// PID Tuning Parameters - Brew Mode
static double BrewKp = DEFAULT_BREW_KP;
static double BrewKi = DEFAULT_BREW_KI;
static double BrewKd = DEFAULT_BREW_KD;
static int brewDelaySeconds = DEFAULT_BREW_DELAY_SECONDS;

// Integral accumulator clamp (IMax) - limits integral windup without restricting P+D
static double IMax = DEFAULT_IMAX;

// Brew mode state tracking (managed inside updatePID)
static bool brewModeActive = false;   // Mirrors state.brewMode but transitions safely
static bool brewDelayPhase = false;   // True during the initial heater-OFF delay
static unsigned long brewDelayStartTime = 0;

// PID Controller Instance
// Parameters: Input, Output, Setpoint, Kp, Ki, Kd, Direction
PID myPID(&pidInput, &pidOutput, &pidSetpoint, Kp, Ki, Kd, DIRECT);

// Hardware Timer for SSR Control
hw_timer_t* pidTimer = NULL;

// ISR Variables (1-second window, 100Hz update rate)
const unsigned int windowSize = 1000;  // 1000ms = 1 second window
volatile unsigned int isrCounter = 0;   // Counter increments by 10ms each tick
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
        isrCounter += 10;
        if (isrCounter >= windowSize) {
            isrCounter = 0;
        }
        return;
    }
    
    // Force relay off for testing (allows cool-down without PID interference)
    if (relayForceOff) {
        digitalWrite(RELAY_PIN, LOW);
        isrCounter += 10;
        if (isrCounter >= windowSize) {
            isrCounter = 0;
        }
        return;
    }
    
    // Time-proportional control (use volatile ISR copy)
    if (pidOutputISR <= isrCounter) {
        digitalWrite(RELAY_PIN, LOW);  // Turn SSR OFF
    } else {
        digitalWrite(RELAY_PIN, HIGH); // Turn SSR ON
    }
    
    isrCounter += 10;  // Increment by 10ms
    
    // Reset counter every 1000ms (1 second window)
    if (isrCounter >= windowSize) {
        isrCounter = 0;
    }
}

void setupControls() {
    // Setup Relay Pin
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    Serial.print("[CONTROLS] Relay pin ");
    Serial.print(RELAY_PIN);
    Serial.println(" initialized to LOW");
    
    // NOTE: PID settings already loaded from storage in main setup()
    
    // Configure PID Controller
    pidSetpoint = state.setTemp;  // Already loaded from storage
    myPID.SetOutputLimits(0, windowSize);  // Output range: 0-1000ms
    myPID.SetSampleTime(100);              // Calculate every 100ms (10Hz)
    myPID.SetMode(AUTOMATIC);              // Enable PID controller
    myPID.SetTunings(Kp, Ki, Kd);          // Apply loaded tunings
    myPID.SetIntegratorLimits(0, IMax);    // Cap integral accumulator to prevent windup
    
    // Optional: Set derivative smoothing (EMA filter on derivative term)
    // This reduces noise on the D-term without filtering the main temperature input
    // Note: This requires a modified PID library that supports SetSmoothingFactor
    // If your library doesn't support it, comment out the next line
    // myPID.SetSmoothingFactor(0.6);
    
    Serial.println("[CONTROLS] PID configured:");
    Serial.print("  Kp = "); Serial.println(Kp, 1);
    Serial.print("  Ki = "); Serial.println(Ki, 2);
    Serial.print("  Kd = "); Serial.println(Kd, 1);
    Serial.print("  Sample Time = 100ms (10Hz)");
    Serial.println();
    Serial.print("  SSR Window = 1000ms (1Hz)");
    Serial.println();
    
    // Setup Hardware Timer (ESP32 Arduino Core 3.x API)
    // Timer at 100Hz (every 10ms) for SSR control
    Serial.println("[CONTROLS] Initializing hardware timer...");
    
    // Create timer with 1MHz base frequency (default ESP32 timer clock / 80)
    pidTimer = timerBegin(1000000);  // 1MHz timer base frequency
    if (pidTimer == NULL) {
        Serial.println("[CONTROLS] ERROR: Failed to create timer!");
        return;
    }
    
    timerAttachInterrupt(pidTimer, &onPIDTimer);  // Attach ISR callback
    timerAlarm(pidTimer, 10000, true, 0);  // Alarm every 10000 us (10ms), auto-repeat
    
    Serial.println("[CONTROLS] Hardware timer configured");
    
    // Verify timer is ticking
    delay(100);
    if (isrTickCount > 0) {
        Serial.printf("[CONTROLS] Timer verified! ISR running at ~%lu Hz\n", isrTickCount * 10);
    } else {
        Serial.println("[CONTROLS] WARNING: Timer ISR not executing!");
    }
    
    Serial.println("[CONTROLS] Control system ready!");
}

void updatePID() {
    // Read state with mutex protection
    STATE_LOCK();
    bool sensorErr = state.sensorError;
    float currentTemp = state.currentTemp;
    float setTemp = state.setTemp;
    int sensorFailures = state.consecutiveSensorFailures;
    bool brewMode = state.brewMode;
    STATE_UNLOCK();
    
    // CRITICAL SAFETY CHECK #1: Sensor Error
    if (sensorErr) {
        if (!emergencyStopActive) {
            emergencyStopActive = true;
            pidOutput = 0;
            pidOutputISR = 0;
            digitalWrite(RELAY_PIN, LOW);
            Serial.println("[CONTROLS] !!! EMERGENCY STOP: Sensor error detected !!!");
        }
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        return;
    }
    
    // CRITICAL SAFETY CHECK #2: Temperature out of plausible range
    if (currentTemp < 5.0 || currentTemp > EMERGENCY_STOP_TEMP) {
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
    if (emergencyStopActive && currentTemp < (setTemp + 5.0)) {
        emergencyStopActive = false;
        Serial.println("[CONTROLS] Emergency stop cleared - temperature safe");
    }

    // --- BREW MODE TRANSITION LOGIC ---
    if (brewMode && !brewModeActive) {
        // Brew mode just activated: start heater-OFF delay phase
        brewModeActive = true;
        brewDelayPhase = true;
        brewDelayStartTime = millis();
        // Force heater off for bumpless entry: zero pidOutput before going AUTOMATIC
        myPID.SetMode(MANUAL);
        pidOutput = 0;
        myPID.SetMode(AUTOMATIC);
        Serial.printf("[CONTROLS] Brew mode ON - heater-OFF delay started (%ds)\n", brewDelaySeconds);
    } else if (!brewMode && brewModeActive) {
        // Brew mode deactivated: switch back to normal heating PID
        brewModeActive = false;
        brewDelayPhase = false;
        myPID.SetMode(MANUAL);
        myPID.SetMode(AUTOMATIC);
        myPID.SetTunings(Kp, Ki, Kd);
        Serial.println("[CONTROLS] Brew mode OFF - back to heating PID");
    }

    // Check if delay phase has elapsed
    if (brewModeActive && brewDelayPhase) {
        if (millis() - brewDelayStartTime >= (unsigned long)brewDelaySeconds * 1000UL) {
            brewDelayPhase = false;
            // Bumpless transfer into brew PID (pidOutput already 0 from delay phase)
            myPID.SetMode(MANUAL);
            myPID.SetMode(AUTOMATIC);
            myPID.SetTunings(BrewKp, BrewKi, BrewKd);
            Serial.println("[CONTROLS] Brew delay complete - switched to brew PID");
        }
    }

    // During delay phase: keep heater OFF and skip PID compute
    if (brewModeActive && brewDelayPhase) {
        pidOutput = 0;
        pidOutputISR = 0;
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        static unsigned long lastDelayDebug = 0;
        if (millis() - lastDelayDebug > 1000) {
            unsigned long elapsed = (millis() - brewDelayStartTime) / 1000;
            Serial.printf("[BREW DELAY] %lus/%ds | Temp: %.1f°C | Heater: OFF\n",
                         elapsed, brewDelaySeconds, currentTemp);
            lastDelayDebug = millis();
        }
        return;
    }

    // --- NORMAL / BREW PID COMPUTE ---
    pidInput = currentTemp;
    pidSetpoint = setTemp;

    double error = setTemp - currentTemp;

    if (brewModeActive) {
        // Brew PID: integral is needed to fight continuous heat extraction from cold water.
        // Tunings are already set to BrewKp/BrewKi/BrewKd from the transition above.
        myPID.Compute();
    } else {
        // Full PID with integral always active.
        // PID_v1's built-in output clamping (0..windowSize) prevents true windup:
        // when output saturates at 0 (above setpoint), accumulation stops naturally.
        myPID.SetTunings(Kp, Ki, Kd);
        myPID.Compute();
    }
    
    // Copy to volatile ISR variable (atomic update for ISR to read)
    pidOutputISR = pidOutput;
    
    // Store output in global state for display/debugging (thread-safe)
    STATE_LOCK();
    state.pidOutput = pidOutput;
    STATE_UNLOCK();
    
    // Console output every 1 second
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 1000) {
        float dutyCycle = (pidOutput / 10.0);
        const char* modeStr = brewModeActive ? "BREW" : "HEAT";
        Serial.printf("[%s] SetTemp: %.1f\u00b0C | Temp: %.1f\u00b0C | PID: %.0f | Duty: %.1f%%",
                      modeStr, setTemp, currentTemp, pidOutput, dutyCycle);
        if (sensorFailures > 0) {
            Serial.printf(" [WARN: %d sensor failures]", sensorFailures);
        }
        static unsigned long lastIsrCount = 0;
        unsigned long currentIsrCount = isrTickCount;
        int relayState = digitalRead(RELAY_PIN);
        Serial.printf(" [ISR: %lu ticks/s, Relay: %s]",
                     (currentIsrCount - lastIsrCount), relayState ? "ON" : "OFF");
        lastIsrCount = currentIsrCount;
        Serial.println();
        lastDebug = millis();
    }
}

void resetPIDMemory() {
    // Zero the integral accumulator by reinitialising PID with output forced to 0.
    // PID_v1 sets outputSum = pidOutput on SetMode(AUTOMATIC), so we zero pidOutput first.
    pidOutput = 0.0;
    myPID.SetMode(MANUAL);
    myPID.SetMode(AUTOMATIC);
    pidOutputISR = 0;
    Serial.println("[CONTROLS] PID memory reset - integral accumulator zeroed");
}

void emergencyStop() {
    emergencyStopActive = true;
    pidOutput = 0;       // Force PID output to zero
    pidOutputISR = 0;    // Update ISR copy
    digitalWrite(RELAY_PIN, LOW);  // Ensure relay is OFF
    
    Serial.println();
    Serial.println("!!! EMERGENCY STOP ACTIVATED !!!");
    Serial.print("Temperature exceeded safe limit: ");
    Serial.print(state.currentTemp, 1);
    Serial.print("°C (limit: ");
    Serial.print(EMERGENCY_STOP_TEMP, 1);
    Serial.println("°C)");
    Serial.println("Heater disabled. Cool down machine before restarting.");
    Serial.println();
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
    Serial.printf("[CONTROLS] Brew mode %s\n", active ? "ACTIVATED" : "DEACTIVATED");
}

bool isBrewModeActive() {
    STATE_LOCK();
    bool active = state.brewMode;
    STATE_UNLOCK();
    return active;
}

bool isBrewDelayPhase() {
    return brewDelayPhase;
}

void setBrewPIDTunings(double kp, double ki, double kd, int delaySeconds) {
    BrewKp = kp;
    BrewKi = ki;
    BrewKd = kd;
    brewDelaySeconds = delaySeconds;
    // Apply immediately if we are currently in the brew PID phase (not delay)
    if (brewModeActive && !brewDelayPhase) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd);
    }
    saveBrewSettingsToStorage();
    Serial.printf("[CONTROLS] Brew PID updated: Kp=%.1f, Ki=%.2f, Kd=%.1f, Delay=%ds\n",
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
    if (brewModeActive && !brewDelayPhase) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd);
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
    Serial.println("[STORAGE] Brew settings saved to NVS");
}

// ========== HEATING PID API FUNCTIONS ==========

void setPIDTunings(double kp, double ki, double kd) {
    Kp = kp;
    Ki = ki;
    Kd = kd;
    myPID.SetTunings(Kp, Ki, Kd);
    savePIDToStorage();  // Save to persistent storage
    Serial.printf("[CONTROLS] PID tunings updated: Kp=%.1f, Ki=%.2f, Kd=%.1f (saved to NVS)\n", Kp, Ki, Kd);
}

void getPIDTunings(double &kp, double &ki, double &kd) {
    kp = Kp;
    ki = Ki;
    kd = Kd;
}

void setIMax(double imax) {
    if (imax < 0) imax = 0;
    if (imax > windowSize) imax = windowSize;
    IMax = imax;
    myPID.SetIntegratorLimits(0, IMax);
    savePIDToStorage();
    Serial.printf("[CONTROLS] IMax updated: %.0f (saved to NVS)\n", IMax);
}

double getIMax() {
    return IMax;
}

void setTargetTemp(double temp) {
    if (temp >= 0.0 && temp <= 120.0) {
        STATE_LOCK();
        state.setTemp = temp;
        STATE_UNLOCK();
        pidSetpoint = temp;
        savePIDToStorage();  // Save to persistent storage
        Serial.printf("[CONTROLS] Target temperature set to %.1f°C (saved to NVS)\n", temp);
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
    
    // Load target temperature
    state.setTemp = preferences.getDouble("targetTemp", DEFAULT_TARGET_TEMP);
    pidSetpoint = state.setTemp;

    // Load brew mode settings
    BrewKp = preferences.getDouble("brewKp", DEFAULT_BREW_KP);
    BrewKi = preferences.getDouble("brewKi", DEFAULT_BREW_KI);
    BrewKd = preferences.getDouble("brewKd", DEFAULT_BREW_KD);
    brewDelaySeconds = preferences.getInt("brewDelay", DEFAULT_BREW_DELAY_SECONDS);

    // Load IMax (integral accumulator clamp)
    IMax = preferences.getDouble("iMax", DEFAULT_IMAX);
    
    preferences.end();
    
    Serial.println("[STORAGE] PID settings loaded from NVS:");
    Serial.printf("  Kp = %.1f, Ki = %.2f, Kd = %.1f, Target = %.1f\u00b0C, IMax = %.0f\n", Kp, Ki, Kd, state.setTemp, IMax);
    Serial.printf("  Brew Kp = %.1f, Ki = %.2f, Kd = %.1f, Delay = %ds\n",
                  BrewKp, BrewKi, BrewKd, brewDelaySeconds);
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
    preferences.putDouble("targetTemp", state.setTemp);
    preferences.putDouble("iMax", IMax);
    
    preferences.end();
    
    Serial.println("[STORAGE] PID settings saved to NVS");
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
    state.setTemp = DEFAULT_TARGET_TEMP;
    pidSetpoint = state.setTemp;
    
    // Update PID controller
    myPID.SetTunings(Kp, Ki, Kd);
    IMax = DEFAULT_IMAX;
    myPID.SetIntegratorLimits(0, IMax);
    
    // Save to storage
    savePIDToStorage();
    
    Serial.println("[STORAGE] PID settings reset to factory defaults:");
    Serial.printf("  Kp = %.1f\n", Kp);
    Serial.printf("  Ki = %.2f\n", Ki);
    Serial.printf("  Kd = %.1f\n", Kd);
    Serial.printf("  Target = %.1f°C\n", state.setTemp);
}