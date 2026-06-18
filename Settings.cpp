#include "Settings.h"
#include <Preferences.h>

Settings          settings;
SemaphoreHandle_t settingsMutex = NULL;

static Preferences prefs;

// --------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------
static float    clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double   clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Per-preset NVS key, e.g. "p0_ct". Caller passes a 3-char suffix.
static void presetKey(char* out, uint8_t i, const char* suffix) {
    snprintf(out, 8, "p%u_%s", (unsigned)i, suffix);
}

// --------------------------------------------------------------------
// Clamp every field to its range + enforce cross-field coherence.
// Assumes the caller holds settingsMutex.
// --------------------------------------------------------------------
static void sanitizeLocked() {
    settings.heatingKp = clampd(settings.heatingKp, PID_KP_MIN, PID_KP_MAX);
    settings.heatingKi = clampd(settings.heatingKi, PID_KI_MIN, PID_KI_MAX);
    settings.heatingKd = clampd(settings.heatingKd, PID_KD_MIN, PID_KD_MAX);
    settings.brewKp    = clampd(settings.brewKp,    PID_KP_MIN, PID_KP_MAX);
    settings.brewKi    = clampd(settings.brewKi,    PID_KI_MIN, PID_KI_MAX);
    settings.brewKd    = clampd(settings.brewKd,    PID_KD_MIN, PID_KD_MAX);

    settings.tempOffset      = clampf(settings.tempOffset,      TEMP_OFFSET_MIN,       TEMP_OFFSET_MAX);
    settings.coffeeTempMax   = clampf(settings.coffeeTempMax,   COFFEE_TEMP_MAX_MIN,   COFFEE_TEMP_MAX_MAX);
    settings.steamTempMax    = clampf(settings.steamTempMax,    STEAM_TEMP_MAX_MIN,    STEAM_TEMP_MAX_MAX);
    settings.safePressureMax = clampf(settings.safePressureMax, SAFE_PRESSURE_MAX_MIN, SAFE_PRESSURE_MAX_MAX);

    if (settings.activePresetIndex >= NUM_PRESETS) settings.activePresetIndex = 0;

    for (uint8_t i = 0; i < NUM_PRESETS; i++) {
        Preset& p = settings.preset[i];
        p.coffeeTargetTemp   = clampf(p.coffeeTargetTemp,   COFFEE_TARGET_TEMP_MIN, COFFEE_TARGET_TEMP_MAX);
        p.steamTargetTemp    = clampf(p.steamTargetTemp,    STEAM_TARGET_TEMP_MIN,  STEAM_TARGET_TEMP_MAX);
        p.preinfuseMaxMs     = clampu(p.preinfuseMaxMs,     BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
        p.bloomMs            = clampu(p.bloomMs,            BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
        p.preheatMs          = clampu(p.preheatMs,          BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
        p.brewMaxMs          = clampu(p.brewMaxMs,          BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
        p.shotMs             = clampu(p.shotMs,             SHOT_TIME_MIN_MS,       SHOT_TIME_MAX_MS);
        p.preinfuseTargetBar = clampf(p.preinfuseTargetBar, PREINFUSE_TARGET_BAR_MIN, PREINFUSE_TARGET_BAR_MAX);

        // Coherence: limits must sit above targets; shot must cover the boost.
        if (p.coffeeTargetTemp > settings.coffeeTempMax - 10.0f)
            p.coffeeTargetTemp = settings.coffeeTempMax - 10.0f;
        if (p.steamTargetTemp > settings.steamTempMax - 10.0f)
            p.steamTargetTemp = settings.steamTempMax - 10.0f;
        if (p.shotMs < p.brewMaxMs) p.shotMs = p.brewMaxMs;
    }
}

// --------------------------------------------------------------------
// NVS I/O (assume settingsMutex held)
// --------------------------------------------------------------------
static void readNvsLocked() {
    prefs.begin(NVS_NAMESPACE, true);   // read-only

    settings.heatingKp = prefs.getDouble("hKp", DEFAULT_HEATING_KP);
    settings.heatingKi = prefs.getDouble("hKi", DEFAULT_HEATING_KI);
    settings.heatingKd = prefs.getDouble("hKd", DEFAULT_HEATING_KD);
    settings.brewKp    = prefs.getDouble("bKp", DEFAULT_BREW_KP);
    settings.brewKi    = prefs.getDouble("bKi", DEFAULT_BREW_KI);
    settings.brewKd    = prefs.getDouble("bKd", DEFAULT_BREW_KD);

    settings.tempOffset      = prefs.getFloat("off",  DEFAULT_TEMP_OFFSET);
    settings.buzzerMute      = prefs.getBool ("mute", BUZZER_DEFAULT_MUTE);
    settings.coffeeTempMax   = prefs.getFloat("cMax", DEFAULT_COFFEE_TEMP_MAX);
    settings.steamTempMax    = prefs.getFloat("sMax", DEFAULT_STEAM_TEMP_MAX);
    settings.safePressureMax = prefs.getFloat("pMax", DEFAULT_SAFE_PRESSURE_MAX);
    settings.activePresetIndex = prefs.getUChar("actP", 0);

    char k[8];
    for (uint8_t i = 0; i < NUM_PRESETS; i++) {
        Preset& p = settings.preset[i];
        presetKey(k, i, "ct"); p.coffeeTargetTemp   = prefs.getFloat(k, DEFAULT_COFFEE_TARGET_TEMP);
        presetKey(k, i, "st"); p.steamTargetTemp    = prefs.getFloat(k, DEFAULT_STEAM_TARGET_TEMP);
        presetKey(k, i, "pi"); p.preinfuseMaxMs     = prefs.getUInt (k, DEFAULT_PREINFUSE_MAX_MS);
        presetKey(k, i, "bl"); p.bloomMs            = prefs.getUInt (k, DEFAULT_BLOOM_MS);
        presetKey(k, i, "ph"); p.preheatMs          = prefs.getUInt (k, DEFAULT_PREHEAT_MS);
        presetKey(k, i, "bm"); p.brewMaxMs          = prefs.getUInt (k, DEFAULT_BREW_MAX_MS);
        presetKey(k, i, "sh"); p.shotMs             = prefs.getUInt (k, DEFAULT_SHOT_MS);
        presetKey(k, i, "pb"); p.preinfuseTargetBar = prefs.getFloat(k, DEFAULT_PREINFUSE_TARGET_BAR);
    }

    prefs.end();
}

static void writeNvsLocked() {
    prefs.begin(NVS_NAMESPACE, false);  // read-write

    prefs.putDouble("hKp", settings.heatingKp);
    prefs.putDouble("hKi", settings.heatingKi);
    prefs.putDouble("hKd", settings.heatingKd);
    prefs.putDouble("bKp", settings.brewKp);
    prefs.putDouble("bKi", settings.brewKi);
    prefs.putDouble("bKd", settings.brewKd);

    prefs.putFloat("off",  settings.tempOffset);
    prefs.putBool ("mute", settings.buzzerMute);
    prefs.putFloat("cMax", settings.coffeeTempMax);
    prefs.putFloat("sMax", settings.steamTempMax);
    prefs.putFloat("pMax", settings.safePressureMax);
    prefs.putUChar("actP", settings.activePresetIndex);

    char k[8];
    for (uint8_t i = 0; i < NUM_PRESETS; i++) {
        const Preset& p = settings.preset[i];
        presetKey(k, i, "ct"); prefs.putFloat(k, p.coffeeTargetTemp);
        presetKey(k, i, "st"); prefs.putFloat(k, p.steamTargetTemp);
        presetKey(k, i, "pi"); prefs.putUInt (k, p.preinfuseMaxMs);
        presetKey(k, i, "bl"); prefs.putUInt (k, p.bloomMs);
        presetKey(k, i, "ph"); prefs.putUInt (k, p.preheatMs);
        presetKey(k, i, "bm"); prefs.putUInt (k, p.brewMaxMs);
        presetKey(k, i, "sh"); prefs.putUInt (k, p.shotMs);
        presetKey(k, i, "pb"); prefs.putFloat(k, p.preinfuseTargetBar);
    }

    prefs.end();
}

// --------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------
void initSettings() {
    settingsMutex = xSemaphoreCreateMutex();
    loadSettings();
}

void loadSettings() {
    SETTINGS_LOCK();
    readNvsLocked();
    sanitizeLocked();
    SETTINGS_UNLOCK();
}

void saveSettings() {
    SETTINGS_LOCK();
    sanitizeLocked();
    writeNvsLocked();
    SETTINGS_UNLOCK();
}

void resetSettings() {
    SETTINGS_LOCK();
    settings.heatingKp = DEFAULT_HEATING_KP; settings.heatingKi = DEFAULT_HEATING_KI; settings.heatingKd = DEFAULT_HEATING_KD;
    settings.brewKp    = DEFAULT_BREW_KP;    settings.brewKi    = DEFAULT_BREW_KI;    settings.brewKd    = DEFAULT_BREW_KD;
    settings.tempOffset      = DEFAULT_TEMP_OFFSET;
    settings.buzzerMute      = BUZZER_DEFAULT_MUTE;
    settings.coffeeTempMax   = DEFAULT_COFFEE_TEMP_MAX;
    settings.steamTempMax    = DEFAULT_STEAM_TEMP_MAX;
    settings.safePressureMax = DEFAULT_SAFE_PRESSURE_MAX;
    settings.activePresetIndex = 0;
    Preset def = {
        DEFAULT_COFFEE_TARGET_TEMP, DEFAULT_STEAM_TARGET_TEMP,
        DEFAULT_PREINFUSE_MAX_MS, DEFAULT_BLOOM_MS, DEFAULT_PREHEAT_MS,
        DEFAULT_BREW_MAX_MS, DEFAULT_SHOT_MS, DEFAULT_PREINFUSE_TARGET_BAR
    };
    for (uint8_t i = 0; i < NUM_PRESETS; i++) settings.preset[i] = def;
    sanitizeLocked();
    writeNvsLocked();
    SETTINGS_UNLOCK();
}

Settings settingsSnapshot() {
    SETTINGS_LOCK();
    Settings copy = settings;
    SETTINGS_UNLOCK();
    return copy;
}

Preset activePreset() {
    SETTINGS_LOCK();
    Preset p = settings.preset[settings.activePresetIndex];
    SETTINGS_UNLOCK();
    return p;
}

void settingsApply(const Settings& incoming) {
    SETTINGS_LOCK();
    settings = incoming;
    sanitizeLocked();
    writeNvsLocked();
    SETTINGS_UNLOCK();
}

void settingsSetActivePreset(uint8_t index) {
    if (index >= NUM_PRESETS) return;
    SETTINGS_LOCK();
    settings.activePresetIndex = index;
    writeNvsLocked();
    SETTINGS_UNLOCK();
}

void settingsAdjustCoffeeTarget(float deltaC) {
    SETTINGS_LOCK();
    settings.preset[settings.activePresetIndex].coffeeTargetTemp += deltaC;
    sanitizeLocked();
    writeNvsLocked();
    SETTINGS_UNLOCK();
}

void settingsAdjustShotTime(long deltaMs) {
    SETTINGS_LOCK();
    long v = (long)settings.preset[settings.activePresetIndex].shotMs + deltaMs;
    if (v < 0) v = 0;
    settings.preset[settings.activePresetIndex].shotMs = (uint32_t)v;
    sanitizeLocked();
    writeNvsLocked();
    SETTINGS_UNLOCK();
}
