#include "StateMachine.h"
#include "State.h"
#include "Controls.h"
#include "Sensors.h"
#include <Arduino.h>
#include <string.h>

// Tracks how long the current substate (or top-level state) has been active
static unsigned long substateEntryMillis = 0;

// Pending error reason — set before committing to STATE_ERROR
static char pendingErrorReason[64] = "";

// ============================================================
// OUTPUT LOGIC — applied every cycle (idempotent)
// ============================================================
static void applyOutputs(MachineState ms, CoffeeSubstate cs) {
    switch (ms) {

        case STATE_IDLE:
            setHeaterOutput(HEATER_PID);
            setPump(false);
            setValve(false);
            break;

        case STATE_STEAM:
            // Hardware steam thermostat and pulsor board take over;
            // valve routes pulsing water to the brew group
            setHeaterOutput(HEATER_OFF);
            setPump(true);
            setValve(true);
            break;

        case STATE_ERROR:
            // All outputs off — valve open vents pressure into drip tray
            setHeaterOutput(HEATER_OFF);
            setPump(false);
            setValve(false);
            break;

        case STATE_COFFEE:
            switch (cs) {
                case COFFEE_PREINFUSE:
                    // Pump builds pressure slowly through puck
                    setHeaterOutput(HEATER_PID);
                    setPump(true);
                    setValve(true);
                    break;
                case COFFEE_BLOOM:
                    // Pump off — valve holds pressure to soak puck
                    setHeaterOutput(HEATER_PID);
                    setPump(false);
                    setValve(true);  // CRITICAL: must stay ON to hold line pressure
                    break;
                case COFFEE_PREHEAT:
                    // Short full-heat burst to recover block temp before extraction
                    setHeaterOutput(HEATER_FULL_ON);
                    setPump(false);
                    setValve(true);
                    break;
                case COFFEE_BREW_MAX:
                    // Full heat + pump to counter initial temperature dip
                    setHeaterOutput(HEATER_FULL_ON);
                    setPump(true);
                    setValve(true);
                    break;
                case COFFEE_BREW_PID:
                    // Brew PID tunings active (set on substate entry)
                    setHeaterOutput(HEATER_PID);
                    setPump(true);
                    setValve(true);
                    break;
                case COFFEE_DONE:
                    // Valve off relieves puck pressure into drip tray
                    setHeaterOutput(HEATER_PID);
                    setPump(false);
                    setValve(false);
                    break;
                default:
                    break;
            }
            break;
    }
}

// ============================================================
// SETUP
// ============================================================
void setupStateMachine() {
    substateEntryMillis = millis();
    // Boot into IDLE: heater under PID, pump and valve off
    setHeaterOutput(HEATER_PID);
    setPump(false);
    setValve(false);
    Serial.println("[SM] State machine initialized (IDLE)");
}

// ============================================================
// UPDATE — call every 100ms from TaskControl
// ============================================================
void updateStateMachine() {
    // Snapshot all relevant state under one mutex lock
    STATE_LOCK();
    bool       swSteam    = state.swSteam;
    bool       swCoffee   = state.swCoffee;
    float      temp       = state.currentTemp;
    float      pressure   = state.currentPressure;
    MachineState  ms      = state.machineState;
    CoffeeSubstate cs     = state.coffeeSubstate;
    STATE_UNLOCK();

    // Sensor timeout check — outside mutex; both readTemperature() and updateStateMachine()
    // run on the same task (TaskControl, Core 1) so no concurrent access
    bool sensorTimedOut = isSensorTimedOut();

    MachineState   newMs  = ms;
    CoffeeSubstate newCs  = cs;
    bool substateChanged  = false;

    // ----------------------------------------------------------
    // SAFETY TRIP — overrides all normal logic
    // ----------------------------------------------------------
    bool safetyTrip = sensorTimedOut
                   || (temp     > SAFE_TEMP_MAX)
                   || (pressure > SAFE_PRESSURE_MAX);

    if (safetyTrip) {
        if (ms != STATE_ERROR) {
            newMs = STATE_ERROR;
            newCs = SUBSTATE_NONE;
            if (sensorTimedOut) {
                snprintf(pendingErrorReason, sizeof(pendingErrorReason),
                         "Sensor timeout (no reading for %lus)", SENSOR_TIMEOUT_MS / 1000UL);
            } else if (temp > SAFE_TEMP_MAX) {
                snprintf(pendingErrorReason, sizeof(pendingErrorReason),
                         "Over-temp: %.1f\xc2\xb0C (limit %.0f\xc2\xb0C)", temp, (float)SAFE_TEMP_MAX);
            } else {
                snprintf(pendingErrorReason, sizeof(pendingErrorReason),
                         "Over-pressure: %.2f Bar (limit %.0f Bar)", pressure, (float)SAFE_PRESSURE_MAX);
            }
            Serial.printf("[SM] \u2192ERROR: %s\n", pendingErrorReason);
        }
    } else {
        // ----------------------------------------------------------
        // NORMAL TRANSITION LOGIC
        // ----------------------------------------------------------
        switch (ms) {

            // ======================================================
            case STATE_IDLE:
                if (swSteam && swCoffee) {
                    newMs = STATE_ERROR;
                    snprintf(pendingErrorReason, sizeof(pendingErrorReason), "Both switches active");
                    Serial.println("[SM] IDLE\u2192ERROR (both switches active)");
                } else if (swSteam) {
                    newMs = STATE_STEAM;
                    Serial.println("[SM] IDLE→STEAM");
                } else if (swCoffee) {
                    if (temp <= BREW_MAX_TEMP) {
                        newMs = STATE_COFFEE;
                        newCs = COFFEE_PREINFUSE;
                        substateChanged = true;
                        Serial.println("[SM] IDLE→COFFEE (PREINFUSE)");
                    } else {
                        Serial.printf("[SM] IDLE: brew blocked — block too hot (%.1f°C > %.1f°C)\n",
                                      temp, (float)BREW_MAX_TEMP);
                    }
                }
                break;

            // ======================================================
            case STATE_STEAM:
                if (swSteam && swCoffee) {
                    newMs = STATE_ERROR;
                    snprintf(pendingErrorReason, sizeof(pendingErrorReason), "Both switches active");
                    Serial.println("[SM] STEAM\u2192ERROR (both switches active)");
                } else if (!swSteam) {
                    // SW_COFFEE may still be on — state machine drops to IDLE;
                    // IDLE will re-evaluate and block if block is still too hot
                    newMs = STATE_IDLE;
                    Serial.println("[SM] STEAM→IDLE");
                }
                break;

            // ======================================================
            case STATE_ERROR:
                // Require both switches off AND conditions safe before clearing
                if (!swSteam && !swCoffee) {
                    newMs = STATE_IDLE;
                    Serial.println("[SM] ERROR→IDLE (acknowledged, conditions safe)");
                }
                break;

            // ======================================================
            case STATE_COFFEE: {
                // Top-level exits take priority over substate logic
                if (swSteam && swCoffee) {
                    newMs = STATE_ERROR;
                    newCs = SUBSTATE_NONE;
                    snprintf(pendingErrorReason, sizeof(pendingErrorReason), "Both switches active");
                    Serial.println("[SM] COFFEE\u2192ERROR (both switches active)");
                    break;
                }
                if (!swSteam && !swCoffee) {
                    newMs = STATE_IDLE;
                    newCs = SUBSTATE_NONE;
                    Serial.println("[SM] COFFEE→IDLE (switch released)");
                    break;
                }
                if (swSteam && !swCoffee) {
                    newMs = STATE_STEAM;
                    newCs = SUBSTATE_NONE;
                    Serial.println("[SM] COFFEE→STEAM");
                    break;
                }

                // Substate transitions
                unsigned long elapsed = millis() - substateEntryMillis;
                switch (cs) {
                    case COFFEE_PREINFUSE:
                        if (pressure >= PREINFUSE_TARGET_PRESS
                                || elapsed >= PREINFUSE_MAX_TIME_MS) {
                            newCs = COFFEE_BLOOM;
                            substateChanged = true;
                            Serial.printf("[SM] PREINFUSE→BLOOM (%s after %lums)\n",
                                pressure >= PREINFUSE_TARGET_PRESS ? "pressure" : "timeout",
                                elapsed);
                        }
                        break;
                    case COFFEE_BLOOM:
                        if (elapsed >= BLOOM_TIME_MS) {
                            newCs = COFFEE_PREHEAT;
                            substateChanged = true;
                            Serial.println("[SM] BLOOM→PREHEAT");
                        }
                        break;
                    case COFFEE_PREHEAT:
                        if (elapsed >= PREHEAT_TIME_MS) {
                            newCs = COFFEE_BREW_MAX;
                            substateChanged = true;
                            Serial.println("[SM] PREHEAT→BREW_MAX");
                        }
                        break;
                    case COFFEE_BREW_MAX:
                        if (elapsed >= BREW_MAX_TIME_MS) {
                            newCs = COFFEE_BREW_PID;
                            substateChanged = true;
                            Serial.println("[SM] BREW_MAX→BREW_PID");
                        }
                        break;
                    case COFFEE_BREW_PID:
                        if (elapsed >= BREW_PID_MAX_TIME_MS) {
                            newCs = COFFEE_DONE;
                            substateChanged = true;
                            Serial.println("[SM] BREW_PID→DONE");
                        }
                        break;
                    case COFFEE_DONE:
                        // Stays here until SW_COFFEE is released (handled above)
                        break;
                    default:
                        break;
                }
                break;
            }

        } // end switch(ms)
    } // end normal transition

    // ----------------------------------------------------------
    // BREW PID TUNINGS — activate on COFFEE_BREW_PID entry, restore on exit
    // ----------------------------------------------------------
    bool enteringBrewPID = (newMs == STATE_COFFEE && newCs == COFFEE_BREW_PID
                            && cs != COFFEE_BREW_PID);
    bool leavingBrewPID  = (cs == COFFEE_BREW_PID
                            && (newMs != STATE_COFFEE || newCs != COFFEE_BREW_PID));
    if (enteringBrewPID) {
        resetPIDMemory();       // Clear integral windup from BREW_MAX full-on phase
        setBrewPIDActive(true);
    } else if (leavingBrewPID) {
        setBrewPIDActive(false);
    }

    // ----------------------------------------------------------
    // COMMIT STATE CHANGES
    // ----------------------------------------------------------
    bool changed = (newMs != ms) || substateChanged;
    if (changed) {
        substateEntryMillis = millis(); // Reset timer on any state/substate change
        STATE_LOCK();
        state.machineState   = newMs;
        state.coffeeSubstate = newCs;
        if (newMs == STATE_ERROR && ms != STATE_ERROR) {
            strncpy(state.errorReason, pendingErrorReason, sizeof(state.errorReason) - 1);
            state.errorReason[sizeof(state.errorReason) - 1] = '\0';
        } else if (newMs == STATE_IDLE && ms == STATE_ERROR) {
            state.errorReason[0] = '\0'; // Clear reason on acknowledgment
        }
        STATE_UNLOCK();
    }

    // ----------------------------------------------------------
    // APPLY OUTPUTS (idempotent — runs every cycle)
    // ----------------------------------------------------------
    applyOutputs(newMs, newCs);
}
