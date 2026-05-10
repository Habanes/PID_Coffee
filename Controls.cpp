#include "Controls.h"
#include "State.h"
#include "Buzzer.h"
#include <PID_v1.h>
#include <Preferences.h>

// Preferences instance for persistent storage
Preferences preferences;

// Mutex to serialise NVS access — savePIDToStorage() can be called from both
// the Control task (button press) and the WebServer task (API handlers).
static SemaphoreHandle_t preferencesMutex = NULL;

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
static bool brewTuningsActive = false; // True when state machine activates brew PID

// Brew timing parameters (milliseconds — runtime-configurable, defaults from Config.h)
static unsigned long brewPreinfuseMs = PREINFUSE_MAX_TIME_MS;
static unsigned long brewBloomMs     = BLOOM_TIME_MS;
static unsigned long brewPreheatMs   = PREHEAT_TIME_MS;
static unsigned long brewMaxMs       = BREW_MAX_TIME_MS;
static unsigned long brewPidMaxMs    = BREW_PID_MAX_TIME_MS;
static float brewPreinfuseTargetBar  = PREINFUSE_TARGET_PRESS;

// Current heater output mode — set by state machine, consumed by ISR
volatile HeaterMode currentHeaterMode = HEATER_OFF;

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
    isrTickCount++;

    // Safety overrides always cut the heater
    if (emergencyStopActive || relayForceOff || currentHeaterMode == HEATER_OFF) {
        digitalWrite(RELAY_PIN, LOW);
        isrCounter += PID_TIMER_INTERVAL_MS;
        if (isrCounter >= windowSize) isrCounter = 0;
        return;
    }

    // Full-on: bypass time-proportional logic
    if (currentHeaterMode == HEATER_FULL_ON) {
        digitalWrite(RELAY_PIN, HIGH);
        isrCounter += PID_TIMER_INTERVAL_MS;
        if (isrCounter >= windowSize) isrCounter = 0;
        return;
    }

    // HEATER_PID: time-proportional using pidOutputISR (atomic 32-bit copy of pidOutput)
    if (isrCounter >= pidOutputISR) {
        digitalWrite(RELAY_PIN, LOW);
    } else {
        digitalWrite(RELAY_PIN, HIGH);
    }
    isrCounter += PID_TIMER_INTERVAL_MS;
    if (isrCounter >= windowSize) isrCounter = 0;
}

void setupControls() {
    // Setup Relay Pin
    Serial.printf("[CONTROLS] Setting up relay: GPIO%d\n", RELAY_PIN);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[CONTROLS] Relay pin LOW (safe)");

    // Setup Pump and Valve pins (active LOW — low-side transistors; HIGH = OFF)
    Serial.printf("[CONTROLS] Setting up pump GPIO%d, valve GPIO%d\n", PIN_PUMP, PIN_VALVE);
    pinMode(PIN_PUMP, OUTPUT);
    digitalWrite(PIN_PUMP, HIGH);   // active-LOW: HIGH = OFF
    pinMode(PIN_VALVE, OUTPUT);
    digitalWrite(PIN_VALVE, HIGH);  // active-LOW: HIGH = OFF
    Serial.println("[CONTROLS] Pump and valve pins HIGH (safe/off)");

    // Configure PID Controller
    pidSetpoint = state.setTemp;  // Already loaded from storage
    Serial.printf("[CONTROLS] PID setpoint: %.1f°C\n", pidSetpoint);
    Serial.printf("[CONTROLS] Configuring PID: Kp=%.2f Ki=%.3f Kd=%.1f IMax=%.1f EMA=%.2f\n",
                  Kp, Ki, Kd, IMax, emaFactor);
    myPID.SetSampleTime(windowSize);
    myPID.SetOutputLimits(0, windowSize);
    myPID.SetIntegratorLimits(0, IMax);
    myPID.SetSmoothingFactor(emaFactor);
    myPID.SetMode(AUTOMATIC);
    myPID.SetTunings(Kp, Ki, Kd, 1);
    Serial.println("[CONTROLS] PID configured");
    
    // Setup Hardware Timer (1MHz base, 100Hz alarm)
    Serial.printf("[CONTROLS] Starting hardware timer (interval=%dms)...\n", PID_TIMER_INTERVAL_MS);
    pidTimer = timerBegin(1000000);
    if (pidTimer == NULL) {
        Serial.println("[CONTROLS] ERROR: Failed to create timer!");
        return;
    }
    Serial.printf("[CONTROLS] Timer handle: %p — attaching ISR\n", (void*)pidTimer);
    timerAttachInterrupt(pidTimer, &onPIDTimer);
    timerAlarm(pidTimer, PID_TIMER_INTERVAL_MS * 1000UL, true, 0);
    Serial.println("[CONTROLS] Timer alarm set — waiting 100ms to verify ISR fires...");
    
    // Verify timer is ticking
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (isrTickCount > 0) {
        Serial.printf("[CONTROLS] Ready | ISR count=%lu | Kp=%.1f Ki=%.3f Kd=%.1f | Setpoint=%.1f°C\n",
                      isrTickCount, Kp, Ki, Kd, pidSetpoint);
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
    // Over-temp check skipped when heater is already off (e.g. steam mode) — the heater
    // cannot be causing the high temperature, and setting the flag would leave it stuck
    // off after returning to idle until the block cools below EMERGENCY_STOP_TEMP.
    if (currentTemp < TEMP_MIN_VALID ||
        (currentTemp > EMERGENCY_STOP_TEMP && currentHeaterMode != HEATER_OFF)) {
        if (!emergencyStopActive) {
            emergencyStop();
            Serial.printf("[CONTROLS] !!! EMERGENCY STOP: Implausible temperature %.1f\u00b0C !!!\n", currentTemp);
        }
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        return;
    }
    
    // Clear emergency stop once temperature drops back below the trigger with hysteresis
    if (emergencyStopActive && currentTemp < (EMERGENCY_STOP_TEMP - EMERGENCY_STOP_HYSTERESIS)) {
        emergencyStopActive = false;
        Serial.println("[CONTROLS] Emergency stop cleared - temperature safe");
    }

    // GUI emergency off: zero duty cycle immediately and skip PID computation
    if (relayForceOff) {
        pidOutput = 0;
        pidOutputISR = 0;
        STATE_LOCK();
        state.pidOutput = 0;
        STATE_UNLOCK();
        return;
    }

    // PID compute — re-apply heating tunings each cycle to pick up live web UI changes
    // (skipped when brew tunings are active — setBrewPIDActive() manages the switch)
    pidInput = currentTemp;
    pidSetpoint = setTemp;
    if (!brewTuningsActive) {
        myPID.SetTunings(Kp, Ki, Kd, 1);
    }
    myPID.Compute();
    pidOutputISR = (uint32_t)pidOutput;

    STATE_LOCK();
    state.pidOutput = pidOutput;
    STATE_UNLOCK();

    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > CONTROLS_DEBUG_MS) {
        float dutyCycle = (pidOutput / windowSize * 100.0);
        Serial.printf("[PID] %.1f°C → %.1f°C | %.1f%% duty\n", setTemp, currentTemp, dutyCycle);
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
    emergencyStopActive = true;  // ISR reads this volatile flag and cuts relay within 10ms
    pidOutput = 0;
    pidOutputISR = 0;

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

void setHeaterOutput(HeaterMode mode) {
    currentHeaterMode = mode;
}

void setPump(bool on) {
    digitalWrite(PIN_PUMP, on ? LOW : HIGH);   // active-LOW: LOW = ON
    STATE_LOCK();
    state.pumpOn = on;
    STATE_UNLOCK();
}

void setValve(bool on) {
    digitalWrite(PIN_VALVE, on ? LOW : HIGH);  // active-LOW: LOW = ON
    STATE_LOCK();
    state.valveOn = on;
    STATE_UNLOCK();
}

// ========== BREW PID API ==========

void setBrewPIDActive(bool active) {
    brewTuningsActive = active;
    if (active) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd, 1);
        Serial.println("[CONTROLS] Brew PID tunings active");
    } else {
        myPID.SetTunings(Kp, Ki, Kd, 1);
        Serial.println("[CONTROLS] Heating PID tunings restored");
    }
}

void setBrewPIDTunings(double kp, double ki, double kd) {
    BrewKp = kp;
    BrewKi = ki;
    BrewKd = kd;
    if (brewTuningsActive) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd, 1);
    }
    saveBrewSettingsToStorage();
    Serial.printf("[CONTROLS] Brew PID updated: Kp=%.1f Ki=%.3f Kd=%.1f\n", BrewKp, BrewKi, BrewKd);
}

void getBrewPIDTunings(double &kp, double &ki, double &kd) {
    kp = BrewKp;
    ki = BrewKi;
    kd = BrewKd;
}

void resetBrewPIDToDefaults() {
    BrewKp = DEFAULT_BREW_KP;
    BrewKi = DEFAULT_BREW_KI;
    BrewKd = DEFAULT_BREW_KD;
    if (brewTuningsActive) {
        myPID.SetTunings(BrewKp, BrewKi, BrewKd, 1);
    }
    saveBrewSettingsToStorage();
    Serial.println("[CONTROLS] Brew PID reset to factory defaults");
}

void saveBrewSettingsToStorage() {
    xSemaphoreTake(preferencesMutex, portMAX_DELAY);
    preferences.begin("coffee-pid", false);
    preferences.putDouble("brewKp", BrewKp);
    preferences.putDouble("brewKi", BrewKi);
    preferences.putDouble("brewKd", BrewKd);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
}

// ========== BREW TIMING API ==========

unsigned long getPreinfuseMaxMs() { return brewPreinfuseMs; }
unsigned long getBloomMs()        { return brewBloomMs; }
unsigned long getPreheatMs()      { return brewPreheatMs; }
unsigned long getBrewMaxMs()      { return brewMaxMs; }
unsigned long getBrewPidMaxMs()   { return brewPidMaxMs; }

float getPreinfuseTargetBar() { return brewPreinfuseTargetBar; }

void setPreinfuseTargetBar(float bar) {
    if (bar >= 0.5f && bar <= 10.0f) {
        brewPreinfuseTargetBar = bar;
        saveBrewTimingsToStorage();
        Serial.printf("[CONTROLS] Preinfuse target pressure: %.2f Bar\n", brewPreinfuseTargetBar);
    }
}

HeaterMode getHeaterMode() { return currentHeaterMode; }

void setBrewTimings(unsigned long preinfuse, unsigned long bloom, unsigned long preheat,
                    unsigned long brewMax, unsigned long brewPidMax) {
    brewPreinfuseMs = preinfuse;
    brewBloomMs     = bloom;
    brewPreheatMs   = preheat;
    brewMaxMs       = brewMax;
    brewPidMaxMs    = brewPidMax;
    saveBrewTimingsToStorage();
    Serial.printf("[CONTROLS] Brew timings: preinfuse=%lums bloom=%lums preheat=%lums brewMax=%lums brewPid=%lums\n",
                  brewPreinfuseMs, brewBloomMs, brewPreheatMs, brewMaxMs, brewPidMaxMs);
}

void resetBrewTimingsToDefaults() {
    brewPreinfuseMs        = PREINFUSE_MAX_TIME_MS;
    brewBloomMs            = BLOOM_TIME_MS;
    brewPreheatMs          = PREHEAT_TIME_MS;
    brewMaxMs              = BREW_MAX_TIME_MS;
    brewPidMaxMs           = BREW_PID_MAX_TIME_MS;
    brewPreinfuseTargetBar = PREINFUSE_TARGET_PRESS;
    saveBrewTimingsToStorage();
    Serial.println("[CONTROLS] Brew timings reset to factory defaults");
}

void saveBrewTimingsToStorage() {
    xSemaphoreTake(preferencesMutex, portMAX_DELAY);
    preferences.begin("coffee-pid", false);
    preferences.putULong("preinfuseMs",  brewPreinfuseMs);
    preferences.putULong("bloomMs",      brewBloomMs);
    preferences.putULong("preheatMs",    brewPreheatMs);
    preferences.putULong("brewMaxMs",    brewMaxMs);
    preferences.putULong("brewPidMaxMs", brewPidMaxMs);
    preferences.putFloat("preinfuseBar", brewPreinfuseTargetBar);
    preferences.end();
    xSemaphoreGive(preferencesMutex);
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
 * If no values are stored, defaults from Config.h are used.
 */
void loadPIDFromStorage() {
    // Initialise here — this runs at boot before any tasks, so the mutex
    // is guaranteed to exist before any concurrent caller can reach the save functions.
    preferencesMutex = xSemaphoreCreateMutex();
    Serial.println("[STORAGE] Opening NVS namespace 'coffee-pid'...");
    bool opened = preferences.begin("coffee-pid", true); // Read-only mode
    Serial.printf("[STORAGE] NVS open: %s\n", opened ? "OK" : "FAILED (will use defaults)");
    
    // Load PID tunings (use defaults if not found)
    Kp = preferences.getDouble("Kp", DEFAULT_KP);
    Ki = preferences.getDouble("Ki", DEFAULT_KI);
    Kd = preferences.getDouble("Kd", DEFAULT_KD);
    IMax = preferences.getDouble("IMax", DEFAULT_IMAX);
    emaFactor = preferences.getDouble("emaFactor", DEFAULT_EMA_FACTOR);
    
    // Load target temperature
    state.setTemp = preferences.getDouble("targetTemp", DEFAULT_TARGET_TEMP);
    pidSetpoint = state.setTemp;

    // Load brew PID tunings
    BrewKp = preferences.getDouble("brewKp", DEFAULT_BREW_KP);
    BrewKi = preferences.getDouble("brewKi", DEFAULT_BREW_KI);
    BrewKd = preferences.getDouble("brewKd", DEFAULT_BREW_KD);
    setBuzzerMute(preferences.getBool("buzzerMute", BUZZER_MUTE));

    // Load brew timing parameters
    brewPreinfuseMs        = preferences.getULong("preinfuseMs",  PREINFUSE_MAX_TIME_MS);
    brewBloomMs            = preferences.getULong("bloomMs",      BLOOM_TIME_MS);
    brewPreheatMs          = preferences.getULong("preheatMs",    PREHEAT_TIME_MS);
    brewMaxMs              = preferences.getULong("brewMaxMs",    BREW_MAX_TIME_MS);
    brewPidMaxMs           = preferences.getULong("brewPidMaxMs", BREW_PID_MAX_TIME_MS);
    brewPreinfuseTargetBar = preferences.getFloat("preinfuseBar", PREINFUSE_TARGET_PRESS);

    preferences.end();
    Serial.printf("[STORAGE] Loaded | Kp=%.1f Ki=%.3f Kd=%.1f | Target=%.1f°C | BrewKp=%.1f Ki=%.3f Kd=%.1f\n",
                  Kp, Ki, Kd, state.setTemp, BrewKp, BrewKi, BrewKd);
    Serial.printf("[STORAGE] Brew timings: preinfuse=%lu bloom=%lu preheat=%lu brewMax=%lu brewPid=%lu ms\n",
                  brewPreinfuseMs, brewBloomMs, brewPreheatMs, brewMaxMs, brewPidMaxMs);
}

/**
 * @brief Save current PID parameters to ESP32 NVS
 * 
 * Stores values persistently - they survive reboots and power loss.
 */
void savePIDToStorage() {
    xSemaphoreTake(preferencesMutex, portMAX_DELAY);
    preferences.begin("coffee-pid", false);
    preferences.putDouble("Kp", Kp);
    preferences.putDouble("Ki", Ki);
    preferences.putDouble("Kd", Kd);
    preferences.putDouble("IMax", IMax);
    preferences.putDouble("emaFactor", emaFactor);
    STATE_LOCK();
    double setTemp = state.setTemp;
    STATE_UNLOCK();
    preferences.putDouble("targetTemp", setTemp);
    preferences.putBool("buzzerMute", getBuzzerMute());
    preferences.end();
    xSemaphoreGive(preferencesMutex);
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