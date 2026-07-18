#include "Switches.h"
#include "State.h"
#include "Config.h"

#if SIMULATION_MODE
// No physical switches wired - switchSteam/switchCoffee are instead set
// directly by Web.cpp's /api/sim handler (fake toggle buttons in the GUI).
// setupSwitches()/readSwitches() become no-ops; Web becomes the sole writer
// of switchSteam/switchCoffee for this build (still exactly one writer,
// just a different one than in a real build).

void setupSwitches() {}
void readSwitches() {}

#else

// Voltage bands (ascending). Higher voltage = fewer optos active.
enum Band { BAND_BOTH, BAND_STEAM, BAND_COFFEE, BAND_NEITHER };

static Band     committed     = BAND_NEITHER;
static Band     candidate     = BAND_NEITHER;
static uint32_t candidateSince = 0;

static Band decodeBand(int adc) {
    if (adc <= SWITCH_LEVEL_1_ADC) return BAND_BOTH;     // ~0V
    if (adc <= SWITCH_LEVEL_2_ADC) return BAND_STEAM;    // ~0.85V
    if (adc <= SWITCH_LEVEL_3_ADC) return BAND_COFFEE;   // ~1.6V
    return BAND_NEITHER;                                 // ~3.3V
}

void setupSwitches() {
    analogSetPinAttenuation(PIN_SWITCHES, ADC_11db);
    committed      = decodeBand(analogRead(PIN_SWITCHES));   // seed
    candidate      = committed;
    candidateSince = millis();
}

void readSwitches() {
    int   adc = analogRead(PIN_SWITCHES);
    float v   = adc * (3.3f / 4095.0f);
    Band  band = decodeBand(adc);
    uint32_t now = millis();

    // Debounce: a new band must hold for SWITCH_DEBOUNCE_MS before it commits.
    // The ~300 ms steam pulse never stays stable that long, so it is rejected.
    if (band != candidate) {
        candidate      = band;
        candidateSince = now;
    } else if (candidate != committed && (now - candidateSince) >= SWITCH_DEBOUNCE_MS) {
        committed = candidate;
    }

    bool steam  = (committed == BAND_STEAM)  || (committed == BAND_BOTH);
    bool coffee = (committed == BAND_COFFEE) || (committed == BAND_BOTH);

    STATE_LOCK();
    state.switchVoltage = v;
    state.switchSteam   = steam;
    state.switchCoffee  = coffee;
    STATE_UNLOCK();
}

#endif // SIMULATION_MODE
