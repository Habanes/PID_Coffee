#include "Settings.h"
#include <Preferences.h>
#include <string.h>

Settings          settings;
SemaphoreHandle_t settingsMutex = NULL;

// The live/working preset - see Settings.h's PRESET MODEL / LIVE-EDIT MODEL
// notes. Guarded by settingsMutex like everything else here.
static Preset workingPreset;

static Preferences prefs;

// --------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------
static float    clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double   clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Slot 0 = "default", active; every other slot inactive/blank. Shared by
// readNvsLocked's first-boot/layout-mismatch fallback and resetSettings().
static void defaultAllPresets() {
    for (uint8_t i = 0; i < MAX_PRESETS; i++) {
        Preset& p = settings.preset[i];
        p.active = (i == 0);
        if (i == 0) strncpy(p.name, "default", PRESET_NAME_MAX_LEN);
        else        p.name[0] = '\0';
        p.name[PRESET_NAME_MAX_LEN] = '\0';
        p.coffeeTargetTemp   = DEFAULT_COFFEE_TARGET_TEMP;
        p.steamTargetTemp    = DEFAULT_STEAM_TARGET_TEMP;
        p.preinfuseMaxMs     = DEFAULT_PREINFUSE_MAX_MS;
        p.bloomMs            = DEFAULT_BLOOM_MS;
        p.preheatMs          = DEFAULT_PREHEAT_MS;
        p.brewMaxMs          = DEFAULT_BREW_MAX_MS;
        p.shotMs             = DEFAULT_SHOT_MS;
        p.preinfuseTargetBar = DEFAULT_PREINFUSE_TARGET_BAR;
    }
}

// Clamp one preset's brew-process fields in place. Needs the CURRENT global
// safety maxes as the ceiling for the two targets - shared by the per-slot
// sanitize loop below and by every working-copy mutator.
static void sanitizePresetFields(Preset& p, float coffeeTempMax, float steamTempMax) {
    p.coffeeTargetTemp   = clampf(p.coffeeTargetTemp,   COFFEE_TARGET_TEMP_MIN, coffeeTempMax);
    p.steamTargetTemp    = clampf(p.steamTargetTemp,    STEAM_TARGET_TEMP_MIN,  steamTempMax);
    p.preinfuseMaxMs     = clampu(p.preinfuseMaxMs,     BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
    p.bloomMs            = clampu(p.bloomMs,            BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
    p.preheatMs          = clampu(p.preheatMs,          BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
    p.brewMaxMs          = clampu(p.brewMaxMs,          BREW_TIME_MIN_MS,       BREW_TIME_MAX_MS);
    p.shotMs             = clampu(p.shotMs,             SHOT_TIME_MIN_MS,       SHOT_TIME_MAX_MS);
    p.preinfuseTargetBar = clampf(p.preinfuseTargetBar, PREINFUSE_TARGET_BAR_MIN, PREINFUSE_TARGET_BAR_MAX);
    // Coherence: shot must cover the boost. (Target-vs-max headroom is no
    // longer force-clamped here - coffeeTempMax/steamTempMax themselves are
    // the sole ceiling on the corresponding target.)
    if (p.shotMs < p.brewMaxMs) p.shotMs = p.brewMaxMs;
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

    settings.coffeeTempMax   = clampf(settings.coffeeTempMax,   COFFEE_TEMP_MAX_MIN,   COFFEE_TEMP_MAX_MAX);
    settings.steamTempMax    = clampf(settings.steamTempMax,    STEAM_TEMP_MAX_MIN,    STEAM_TEMP_MAX_MAX);
    settings.safePressureMax = clampf(settings.safePressureMax, SAFE_PRESSURE_MAX_MIN, SAFE_PRESSURE_MAX_MAX);
    settings.ecoTargetTemp   = clampf(settings.ecoTargetTemp,   ECO_TARGET_TEMP_MIN,   ECO_TARGET_TEMP_MAX);
    settings.ecoTimeoutMs    = clampu(settings.ecoTimeoutMs,    ECO_TIMEOUT_MS_MIN,    ECO_TIMEOUT_MS_MAX);
    settings.sleepTimeoutMs  = clampu(settings.sleepTimeoutMs,  SLEEP_TIMEOUT_MS_MIN,  SLEEP_TIMEOUT_MS_MAX);
    if (settings.sleepTimeoutMs < settings.ecoTimeoutMs) settings.sleepTimeoutMs = settings.ecoTimeoutMs;

    if (settings.activePresetIndex >= MAX_PRESETS) settings.activePresetIndex = 0;
    if (!settings.preset[settings.activePresetIndex].active) {
        // Fall back to the first active slot. Slot 0 is active by invariant
        // (settingsDeletePreset refuses to remove the last remaining one),
        // so this loop always finds something.
        for (uint8_t i = 0; i < MAX_PRESETS; i++) {
            if (settings.preset[i].active) { settings.activePresetIndex = i; break; }
        }
    }

    for (uint8_t i = 0; i < MAX_PRESETS; i++) {
        settings.preset[i].name[PRESET_NAME_MAX_LEN] = '\0';   // defensive - never trust NVS blindly
        sanitizePresetFields(settings.preset[i], settings.coffeeTempMax, settings.steamTempMax);
    }
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
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

    settings.buzzerMute      = prefs.getBool ("mute", BUZZER_DEFAULT_MUTE);
    settings.encoderInverted = prefs.getBool ("encInv", DEFAULT_ENCODER_INVERTED);
    settings.coffeeTempMax   = prefs.getFloat("cMax", DEFAULT_COFFEE_TEMP_MAX);
    settings.steamTempMax    = prefs.getFloat("sMax", DEFAULT_STEAM_TEMP_MAX);
    settings.safePressureMax = prefs.getFloat("pMax", DEFAULT_SAFE_PRESSURE_MAX);
    settings.ecoTargetTemp   = prefs.getFloat("eco",  DEFAULT_ECO_TARGET_TEMP);
    settings.ecoTimeoutMs    = prefs.getUInt ("ecoT", DEFAULT_ECO_TIMEOUT_MS);
    settings.sleepTimeoutMs  = prefs.getUInt ("slpT", DEFAULT_SLEEP_TIMEOUT_MS);
    settings.activePresetIndex = prefs.getUChar("actP", 0);

    // Whole preset array as one NVS blob (one key, not ~10 keys * MAX_PRESETS)
    // - see the PRESET STORAGE note in Settings.h. getBytesLength()==0 covers
    // first boot; a length mismatch covers a firmware update that changed
    // sizeof(Preset) - both fall back to defaults rather than reading garbage.
    if (prefs.getBytesLength("presets") == sizeof(settings.preset)) {
        prefs.getBytes("presets", (void*)settings.preset, sizeof(settings.preset));
    } else {
        defaultAllPresets();
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

    prefs.putBool ("mute", settings.buzzerMute);
    prefs.putBool ("encInv", settings.encoderInverted);
    prefs.putFloat("cMax", settings.coffeeTempMax);
    prefs.putFloat("sMax", settings.steamTempMax);
    prefs.putFloat("pMax", settings.safePressureMax);
    prefs.putFloat("eco",  settings.ecoTargetTemp);
    prefs.putUInt ("ecoT", settings.ecoTimeoutMs);
    prefs.putUInt ("slpT", settings.sleepTimeoutMs);
    prefs.putUChar("actP", settings.activePresetIndex);

    prefs.putBytes("presets", (const void*)settings.preset, sizeof(settings.preset));

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
    workingPreset = settings.preset[settings.activePresetIndex];
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
    settings.buzzerMute      = BUZZER_DEFAULT_MUTE;
    settings.encoderInverted = DEFAULT_ENCODER_INVERTED;
    settings.coffeeTempMax   = DEFAULT_COFFEE_TEMP_MAX;
    settings.steamTempMax    = DEFAULT_STEAM_TEMP_MAX;
    settings.safePressureMax = DEFAULT_SAFE_PRESSURE_MAX;
    settings.ecoTargetTemp   = DEFAULT_ECO_TARGET_TEMP;
    settings.ecoTimeoutMs    = DEFAULT_ECO_TIMEOUT_MS;
    settings.sleepTimeoutMs  = DEFAULT_SLEEP_TIMEOUT_MS;
    settings.activePresetIndex = 0;

    defaultAllPresets();
    workingPreset = settings.preset[0];

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
    Preset p = workingPreset;
    SETTINGS_UNLOCK();
    return p;
}

// --------------------------------------------------------------------
// Rank helpers - a "rank" is a 0-based position among active slots. Each of
// these is self-contained (takes/releases the lock itself) and must never be
// called from a function that already holds settingsMutex (this mutex isn't
// recursive) - see the internal lifecycle functions below, which iterate
// settings.preset[] directly instead of calling back into these.
// --------------------------------------------------------------------
uint8_t presetRankCount() {
    SETTINGS_LOCK();
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_PRESETS; i++) if (settings.preset[i].active) count++;
    SETTINGS_UNLOCK();
    return count;
}

uint8_t presetSlotForRank(uint8_t rank) {
    SETTINGS_LOCK();
    uint8_t seen = 0;
    uint8_t result = 0;   // fallback: slot 0, always active by invariant
    for (uint8_t i = 0; i < MAX_PRESETS; i++) {
        if (settings.preset[i].active) {
            if (seen == rank) { result = i; break; }
            seen++;
        }
    }
    SETTINGS_UNLOCK();
    return result;
}

int8_t presetRankForSlot(uint8_t slot) {
    SETTINGS_LOCK();
    int8_t rank = -1;
    if (slot < MAX_PRESETS && settings.preset[slot].active) {
        uint8_t r = 0;
        for (uint8_t i = 0; i < slot; i++) if (settings.preset[i].active) r++;
        rank = (int8_t)r;
    }
    SETTINGS_UNLOCK();
    return rank;
}

int8_t presetActiveRank() {
    SETTINGS_LOCK();
    uint8_t activeSlot = settings.activePresetIndex;
    SETTINGS_UNLOCK();
    return presetRankForSlot(activeSlot);   // re-locks itself - fine, released above first
}

// --------------------------------------------------------------------
// Global settings apply (web) - GLOBAL fields only. Preset lifecycle and
// per-preset field edits go through the dedicated functions below instead;
// `incoming.preset[]`/`activePresetIndex` are deliberately ignored here so a
// stale snapshot round-tripped through the web GUI can never clobber them.
// --------------------------------------------------------------------
void settingsApply(const Settings& incoming) {
    SETTINGS_LOCK();
    settings.heatingKp = incoming.heatingKp;
    settings.heatingKi = incoming.heatingKi;
    settings.heatingKd = incoming.heatingKd;
    settings.brewKp    = incoming.brewKp;
    settings.brewKi    = incoming.brewKi;
    settings.brewKd    = incoming.brewKd;
    settings.buzzerMute      = incoming.buzzerMute;
    settings.encoderInverted = incoming.encoderInverted;
    settings.coffeeTempMax   = incoming.coffeeTempMax;
    settings.steamTempMax    = incoming.steamTempMax;
    settings.safePressureMax = incoming.safePressureMax;
    settings.ecoTargetTemp   = incoming.ecoTargetTemp;
    settings.ecoTimeoutMs    = incoming.ecoTimeoutMs;
    settings.sleepTimeoutMs  = incoming.sleepTimeoutMs;
    sanitizeLocked();
    writeNvsLocked();
    SETTINGS_UNLOCK();
}

// --------------------------------------------------------------------
// Working-copy edits - live, NOT persisted. This is what BrewStateMachine.cpp
// actually runs on (via activePreset()); nothing here touches a stored slot.
// --------------------------------------------------------------------
void settingsStageWorkingPreset(const Preset& incoming) {
    SETTINGS_LOCK();
    workingPreset.coffeeTargetTemp   = incoming.coffeeTargetTemp;
    workingPreset.steamTargetTemp    = incoming.steamTargetTemp;
    workingPreset.preinfuseMaxMs     = incoming.preinfuseMaxMs;
    workingPreset.bloomMs            = incoming.bloomMs;
    workingPreset.preheatMs          = incoming.preheatMs;
    workingPreset.brewMaxMs          = incoming.brewMaxMs;
    workingPreset.shotMs             = incoming.shotMs;
    workingPreset.preinfuseTargetBar = incoming.preinfuseTargetBar;
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
    SETTINGS_UNLOCK();
}

void settingsAdjustCoffeeTarget(float deltaC) {
    SETTINGS_LOCK();
    workingPreset.coffeeTargetTemp += deltaC;
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
    SETTINGS_UNLOCK();
}

void settingsAdjustPreinfuseMax(long deltaMs) {
    SETTINGS_LOCK();
    long v = (long)workingPreset.preinfuseMaxMs + deltaMs;
    if (v < 0) v = 0;
    workingPreset.preinfuseMaxMs = (uint32_t)v;
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
    SETTINGS_UNLOCK();
}

void settingsAdjustBloom(long deltaMs) {
    SETTINGS_LOCK();
    long v = (long)workingPreset.bloomMs + deltaMs;
    if (v < 0) v = 0;
    workingPreset.bloomMs = (uint32_t)v;
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
    SETTINGS_UNLOCK();
}

void settingsAdjustPreheat(long deltaMs) {
    SETTINGS_LOCK();
    long v = (long)workingPreset.preheatMs + deltaMs;
    if (v < 0) v = 0;
    workingPreset.preheatMs = (uint32_t)v;
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
    SETTINGS_UNLOCK();
}

void settingsAdjustBoost(long deltaMs) {
    SETTINGS_LOCK();
    long v = (long)workingPreset.brewMaxMs + deltaMs;
    if (v < 0) v = 0;
    workingPreset.brewMaxMs = (uint32_t)v;
    sanitizePresetFields(workingPreset, settings.coffeeTempMax, settings.steamTempMax);
    SETTINGS_UNLOCK();
}

// --------------------------------------------------------------------
// Preset lifecycle - the only paths that ever touch stored slots.
// --------------------------------------------------------------------
void settingsSelectPresetBySlot(uint8_t slot) {
    SETTINGS_LOCK();
    if (slot < MAX_PRESETS && settings.preset[slot].active) {
        settings.activePresetIndex = slot;
        workingPreset = settings.preset[slot];
        sanitizeLocked();
        writeNvsLocked();
    }
    SETTINGS_UNLOCK();
}

void settingsSaveWorkingAsNewPreset(const char* name) {
    SETTINGS_LOCK();
    int freeSlot = -1;
    for (uint8_t i = 0; i < MAX_PRESETS; i++) {
        if (!settings.preset[i].active) { freeSlot = i; break; }
    }
    if (freeSlot >= 0) {
        Preset& p = settings.preset[freeSlot];
        p.active              = true;
        p.coffeeTargetTemp    = workingPreset.coffeeTargetTemp;
        p.steamTargetTemp     = workingPreset.steamTargetTemp;
        p.preinfuseMaxMs      = workingPreset.preinfuseMaxMs;
        p.bloomMs             = workingPreset.bloomMs;
        p.preheatMs           = workingPreset.preheatMs;
        p.brewMaxMs           = workingPreset.brewMaxMs;
        p.shotMs              = workingPreset.shotMs;
        p.preinfuseTargetBar  = workingPreset.preinfuseTargetBar;
        strncpy(p.name, name, PRESET_NAME_MAX_LEN);
        p.name[PRESET_NAME_MAX_LEN] = '\0';
        settings.activePresetIndex = (uint8_t)freeSlot;
        sanitizeLocked();
        writeNvsLocked();
    }
    // All MAX_PRESETS slots in use: silently no-ops. 20 is a generous
    // ceiling for a single-user machine - treated as won't-happen rather
    // than adding an error-reporting path with no way to test it.
    SETTINGS_UNLOCK();
}

void settingsUpdateSlotFromWorking(uint8_t slot) {
    SETTINGS_LOCK();
    if (slot < MAX_PRESETS && settings.preset[slot].active) {
        Preset& p = settings.preset[slot];
        p.coffeeTargetTemp   = workingPreset.coffeeTargetTemp;
        p.steamTargetTemp    = workingPreset.steamTargetTemp;
        p.preinfuseMaxMs     = workingPreset.preinfuseMaxMs;
        p.bloomMs            = workingPreset.bloomMs;
        p.preheatMs          = workingPreset.preheatMs;
        p.brewMaxMs          = workingPreset.brewMaxMs;
        p.shotMs             = workingPreset.shotMs;
        p.preinfuseTargetBar = workingPreset.preinfuseTargetBar;
        settings.activePresetIndex = slot;   // saving under a preset makes it the active one
        sanitizeLocked();
        writeNvsLocked();
    }
    SETTINGS_UNLOCK();
}

void settingsDeletePreset(uint8_t slot) {
    SETTINGS_LOCK();
    if (slot < MAX_PRESETS && settings.preset[slot].active) {
        uint8_t activeCount = 0;
        for (uint8_t i = 0; i < MAX_PRESETS; i++) if (settings.preset[i].active) activeCount++;
        if (activeCount > 1) {
            settings.preset[slot].active = false;
            if (settings.activePresetIndex == slot) {
                // Fall back to whichever active slot now has rank 0.
                for (uint8_t i = 0; i < MAX_PRESETS; i++) {
                    if (settings.preset[i].active) {
                        settings.activePresetIndex = i;
                        workingPreset = settings.preset[i];
                        break;
                    }
                }
            }
            sanitizeLocked();
            writeNvsLocked();
        }
        // else: refuse - the last remaining preset can't be deleted.
    }
    SETTINGS_UNLOCK();
}

void settingsRenamePreset(uint8_t slot, const char* name) {
    SETTINGS_LOCK();
    if (slot < MAX_PRESETS && settings.preset[slot].active) {
        strncpy(settings.preset[slot].name, name, PRESET_NAME_MAX_LEN);
        settings.preset[slot].name[PRESET_NAME_MAX_LEN] = '\0';
        sanitizeLocked();
        writeNvsLocked();
    }
    SETTINGS_UNLOCK();
}
