#include "BrewStateMachine.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include "Buzzer.h"
#include <Arduino.h>

// Timers (single task = ControlTask, so plain statics are safe).
static uint32_t brewStartMs     = 0;   // COFFEE entry - drives the shot timer
static uint32_t substateEntryMs = 0;   // current coffee substate entry
static uint32_t idleEntryMs     = 0;   // last STATE_IDLE entry - drives the eco timeout
static MachineState errorFromMode = STATE_IDLE;  // mode active when ERROR was
                                                  // entered - clear-check uses
                                                  // its temp limit, not steamTempMax

// valveState: true = energized (closed, holds pressure); false = open (vents).
// pump/valve GPIOs are active LOW.
static void driveOutputs(MachineState ms, CoffeeSubstate cs,
                         float temp, const Preset& p,
                         HeaterMode& hm, bool& pump, bool& valve) {
    switch (ms) {
        case STATE_IDLE:
        case STATE_ECO:                                    // same outputs as IDLE - only the PID target differs
            hm = HEATER_PID;  pump = false; valve = false; break;

        case STATE_STEAM:                                  // bang-bang heater
            hm = (temp < p.steamTargetTemp - STEAM_HYSTERESIS) ? HEATER_FULL_ON
                                                               : HEATER_OFF;
            pump = true;  valve = true;  break;

        case STATE_ERROR:                                  // all off, valve vents
            hm = HEATER_OFF; pump = false; valve = false; break;

        case STATE_HOT_WATER:               // pump through the block, no heat
            hm = HEATER_OFF; pump = true; valve = true; break;

        case STATE_COFFEE:
            switch (cs) {
                case SUB_PREINFUSE: hm = HEATER_PID;     pump = true;  valve = true;  break;
                case SUB_BLOOM:     hm = HEATER_PID;     pump = false; valve = true;  break;
                case SUB_PREHEAT:   hm = HEATER_FULL_ON; pump = false; valve = true;  break;
                case SUB_BREW_MAX:  hm = HEATER_FULL_ON; pump = true;  valve = true;  break;
                case SUB_BREW_PID:  hm = HEATER_PID;     pump = true;  valve = true;  break;
                case SUB_DONE:      hm = HEATER_PID;     pump = false; valve = false; break;
                default:            hm = HEATER_PID;     pump = false; valve = false; break;
            }
            break;
    }
}

void setupBrew() {
    pinMode(PIN_PUMP,  OUTPUT);
    pinMode(PIN_VALVE, OUTPUT);
    digitalWrite(PIN_PUMP,  HIGH);   // active LOW -> HIGH = off
    digitalWrite(PIN_VALVE, HIGH);
    brewStartMs = substateEntryMs = idleEntryMs = millis();
}

void updateBrewStateMachine() {
    // --- snapshot inputs ---
    STATE_LOCK();
    bool  swSteam   = state.switchSteam;
    bool  swCoffee  = state.switchCoffee;
    float temp      = state.currentTemperature;
    float pressure  = state.currentPressure;
    bool  tempFault = state.temperatureSensorError;
    bool  ecoWakeRequested = state.ecoWakeRequested;
    MachineState   ms = state.machineState;
    CoffeeSubstate cs = state.coffeeSubstate;
    STATE_UNLOCK();

    Preset p = activePreset();

    SETTINGS_LOCK();
    float coffeeTempMax   = settings.coffeeTempMax;
    float steamTempMax    = settings.steamTempMax;
    float safePressureMax = settings.safePressureMax;
    float ecoTargetTemp   = settings.ecoTargetTemp;
    uint32_t ecoTimeoutMs = settings.ecoTimeoutMs;
    SETTINGS_UNLOCK();

    uint32_t now = millis();
    MachineState   newMs = ms;
    CoffeeSubstate newCs = cs;
    ErrorReason    err   = ERR_NONE;

    // Over-temp limit depends on the mode (IDLE/STEAM tolerate the steam range).
    float tempLimit = (ms == STATE_COFFEE) ? coffeeTempMax : steamTempMax;

    // Debounce the over-limit trips: the condition must persist before it trips
    // ERROR, so a brief spike (ADC noise / overshoot) is ignored. Sensor fault
    // and both-switches are already debounced upstream, so they trip at once.
    static bool     overTempActive = false, overPressActive = false;
    static uint32_t overTempSince  = 0,     overPressSince  = 0;
    bool overTempNow  = (temp > tempLimit);
#if HAS_PRESSURE_SENSOR
    bool overPressNow = (pressure > safePressureMax);
#else
    bool overPressNow = false;   // no sensor - currentPressure stays 0, never trips
#endif
    if (overTempNow)  { if (!overTempActive)  { overTempActive  = true; overTempSince  = now; } }
    else                overTempActive  = false;
    if (overPressNow) { if (!overPressActive) { overPressActive = true; overPressSince = now; } }
    else                overPressActive = false;
    bool overTempTrip  = overTempActive  && (now - overTempSince  >= OVERTEMP_TRIP_MS);
    bool overPressTrip = overPressActive && (now - overPressSince >= OVERPRESSURE_TRIP_MS);

    if (ms != STATE_ERROR) {
        // --- safety trips override normal logic ---
        if (tempFault)                { newMs = STATE_ERROR; err = ERR_TEMP_SENSOR; }
        else if (overTempTrip)        { newMs = STATE_ERROR; err = ERR_OVER_TEMP; }
        else if (overPressTrip)       { newMs = STATE_ERROR; err = ERR_OVER_PRESSURE; }
        else if (swSteam && swCoffee) {
            // Both switches -> Hot Water: a legitimate mode, not an error.
            // Interrupts any active COFFEE/STEAM run immediately.
            newMs = STATE_HOT_WATER; newCs = SUB_NONE;
        } else {
            // --- normal transitions ---
            switch (ms) {
                case STATE_IDLE:
                    if (swSteam) {
                        newMs = STATE_STEAM;
                    } else if (swCoffee && temp <= BREW_READY_TEMP) {
                        newMs = STATE_COFFEE; newCs = SUB_PREINFUSE;
                        brewStartMs = substateEntryMs = now;
                    } else if (!swCoffee && (now - idleEntryMs) >= ecoTimeoutMs) {
                        // Guarded on !swCoffee so eco doesn't kick in while the
                        // switch is actively held (e.g. blocked by the too-hot gate).
                        newMs = STATE_ECO;
                    }
                    break;

                case STATE_STEAM:
                    if (!swSteam) newMs = STATE_IDLE;   // coffee (if on) re-evaluated in IDLE
                    break;

                case STATE_COFFEE: {
                    if (!swSteam && !swCoffee)      { newMs = STATE_IDLE;  newCs = SUB_NONE; break; }
                    if (swSteam)                    { newMs = STATE_STEAM; newCs = SUB_NONE; break; }

                    uint32_t brewElapsed = now - brewStartMs;
                    uint32_t subElapsed  = now - substateEntryMs;

                    if (brewElapsed >= p.shotMs) {      // global shot timer wins
                        newCs = SUB_DONE;
                    } else switch (cs) {
                        case SUB_PREINFUSE:
                            if (pressure >= p.preinfuseTargetBar || subElapsed >= p.preinfuseMaxMs)
                                { newCs = SUB_BLOOM;    substateEntryMs = now; }
                            break;
                        case SUB_BLOOM:
                            if (subElapsed >= p.bloomMs)   { newCs = SUB_PREHEAT;  substateEntryMs = now; }
                            break;
                        case SUB_PREHEAT:
                            if (subElapsed >= p.preheatMs) { newCs = SUB_BREW_MAX; substateEntryMs = now; }
                            break;
                        case SUB_BREW_MAX:
                            if (subElapsed >= p.brewMaxMs) { newCs = SUB_BREW_PID; substateEntryMs = now; }
                            break;
                        default: break;                 // BREW_PID / DONE: shot timer ends it
                    }
                    break;
                }

                case STATE_HOT_WATER:
                    // Same pattern as STEAM's exit: drop to whichever single switch
                    // (if any) remains active, straight to that mode - no gesture needed.
                    if (!swSteam && !swCoffee)      { newMs = STATE_IDLE;  newCs = SUB_NONE; }
                    else if (swSteam)               { newMs = STATE_STEAM; newCs = SUB_NONE; }
                    else if (swCoffee) {
                        newMs = STATE_COFFEE; newCs = SUB_PREINFUSE;
                        brewStartMs = substateEntryMs = now;
                    }
                    break;

                case STATE_ECO:
                    // A switch goes straight to the matching mode, same as HOT_WATER's
                    // exit - no gate, no IDLE hop; the user is trusted not to brew at
                    // the (cold) eco target if they don't mean to. (Both-switches is
                    // already routed to HOT_WATER by the outer check above, so only
                    // a single switch can be active here.)
                    if (swSteam)                    { newMs = STATE_STEAM;     newCs = SUB_NONE; }
                    else if (swCoffee) {
                        newMs = STATE_COFFEE; newCs = SUB_PREINFUSE;
                        brewStartMs = substateEntryMs = now;
                    } else if (ecoWakeRequested) {
                        newMs = STATE_IDLE;
                    }
                    break;

                default: break;
            }
        }
        if (newMs == STATE_ERROR) errorFromMode = ms;
    } else {
        // --- in ERROR: clear only when both switches off AND fault gone ---
        // Use the temp limit of the mode that actually tripped ERROR, not
        // always steamTempMax - a COFFEE-mode over-temp trip (coffeeTempMax)
        // must cool down against coffeeTempMax, not the much looser steam limit.
        float clearTempLimit = (errorFromMode == STATE_COFFEE) ? coffeeTempMax : steamTempMax;
        bool tempOk   = temp < (clearTempLimit - ERROR_CLEAR_HYSTERESIS);
#if HAS_PRESSURE_SENSOR
        bool pressOk  = pressure < safePressureMax;
#else
        bool pressOk  = true;   // no sensor - never blocks the clear
#endif
        bool sensorOk = !tempFault;
        if (!swSteam && !swCoffee && tempOk && pressOk && sensorOk) {
            newMs = STATE_IDLE; newCs = SUB_NONE;
        }
    }

    // Reset the eco-countdown clock on every fresh arrival into IDLE (COFFEE
    // done, STEAM/HOT_WATER switch released, ERROR cleared, ECO woken) - NOT
    // just on menu-browsing while already idle.
    if (newMs == STATE_IDLE && ms != STATE_IDLE) idleEntryMs = now;

    // --- outputs from the new state ---
    float target = (newMs == STATE_STEAM) ? p.steamTargetTemp
                  : (newMs == STATE_ECO)  ? ecoTargetTemp
                  :                         p.coffeeTargetTemp;
    HeaterMode hm; bool pump, valve;
    driveOutputs(newMs, newCs, temp, p, hm, pump, valve);

    digitalWrite(PIN_PUMP,  pump  ? LOW : HIGH);   // active LOW
    digitalWrite(PIN_VALVE, valve ? LOW : HIGH);

    uint32_t brewElapsed = (newMs == STATE_COFFEE) ? (now - brewStartMs) : 0;

    STATE_LOCK();
    state.machineState             = newMs;
    state.coffeeSubstate           = newCs;
    state.currentTargetTemperature = target;
    state.heaterMode               = hm;
    state.pumpState                = pump;
    state.valveState               = valve;
    state.brewTimerElapsedMs       = brewElapsed;
    if (newMs == STATE_ERROR && ms != STATE_ERROR)      state.errorReason = err;
    else if (newMs == STATE_IDLE && ms == STATE_ERROR)  state.errorReason = ERR_NONE;
    if (ms == STATE_ECO) state.ecoWakeRequested = false;   // consumed this cycle
    STATE_UNLOCK();

    // --- buzzer: mode enter / exit (error siren handled by the UI task) ---
    // ECO is deliberately NOT in this active-mode set: auto-entry is silent
    // (unattended, could fire overnight) and waking gets a plain click below,
    // not the full mode jingle reserved for genuinely active modes.
    bool wasActive = (ms    == STATE_COFFEE || ms    == STATE_STEAM || ms    == STATE_HOT_WATER);
    bool nowActive = (newMs == STATE_COFFEE || newMs == STATE_STEAM || newMs == STATE_HOT_WATER);
    if (nowActive && !wasActive)      buzzerPlay(SND_MODE_ENTER);
    else if (!nowActive && wasActive && newMs == STATE_IDLE) buzzerPlay(SND_MODE_EXIT);
    if (ms == STATE_ECO && newMs == STATE_IDLE) buzzerPlay(SND_CLICK);

    if (newMs != ms) {
        Serial.printf("[SM] %s -> %s\n", machineStateText(ms), machineStateText(newMs));
    }
}
