#ifndef SETTINGS_H
#define SETTINGS_H

// =====================================================================
// Persistent settings: global config + 3 brew presets.
// Written ONLY via the config layer (web GUI / 7-seg menu), IDLE-only,
// always clamped to the *_MIN/*_MAX ranges in Config.h and persisted to NVS.
// See ../Architecture.txt "SETTINGS".
// =====================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

struct Preset {
    float    coffeeTargetTemp;
    float    steamTargetTemp;
    uint32_t preinfuseMaxMs;
    uint32_t bloomMs;
    uint32_t preheatMs;
    uint32_t brewMaxMs;
    uint32_t shotMs;            // TOTAL brew time (continuous from start)
    float    preinfuseTargetBar;
};

struct Settings {
    // Heating PID
    double heatingKp, heatingKi, heatingKd;
    // Brew PID (Ki disabled during brew)
    double brewKp, brewKi, brewKd;
    // Global
    bool    buzzerMute;
    float   coffeeTempMax;      // ERROR in COFFEE
    float   steamTempMax;       // ERROR in IDLE/STEAM + ISR cutoff
    float   safePressureMax;    // ERROR all modes
    float   ecoTargetTemp;      // PID target while in ECO
    uint32_t ecoTimeoutMs;      // continuous IDLE time before auto-entering ECO
    uint8_t activePresetIndex;
    // Presets
    Preset  preset[NUM_PRESETS];
};

extern Settings settings;
extern SemaphoreHandle_t settingsMutex;

#define SETTINGS_LOCK()   xSemaphoreTake(settingsMutex, portMAX_DELAY)
#define SETTINGS_UNLOCK() xSemaphoreGive(settingsMutex)

void initSettings();   // create mutex + load from NVS (call once at boot)
void loadSettings();   // NVS -> settings (DEFAULT_* fallback) + sanitize
void saveSettings();   // sanitize + settings -> NVS
void resetSettings();  // restore all DEFAULT_* + persist

// Consistent copies for readers.
Settings settingsSnapshot();
Preset   activePreset();

// Config-layer edits (lock + clamp + persist internally).
// Caller must ensure machineState == IDLE before calling any of these -
// not enforced here (would give Settings.cpp a dependency on State.h).
void settingsApply(const Settings& incoming);   // web: store whole struct
void settingsSetActivePreset(uint8_t index);     // menu: PRESET view
void settingsAdjustCoffeeTarget(float deltaC);   // menu: SET_COFFEE view
void settingsAdjustShotTime(long deltaMs);       // menu: TIMER view

#endif // SETTINGS_H
