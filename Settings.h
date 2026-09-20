#ifndef SETTINGS_H
#define SETTINGS_H

// =====================================================================
// Persistent settings: global config + a pool of brew presets.
// Written ONLY via the config layer (web GUI / 7-seg menu), IDLE-only,
// always clamped to the *_MIN/*_MAX ranges in Config.h and persisted to NVS.
// See ../Architecture.txt "SETTINGS".
//
// PRESET MODEL: MAX_PRESETS fixed slots, each either active or not (see
// Preset::active below). Only active slots are ever shown, in a "rank"
// (0-based position among active slots, recomputed fresh every time - see
// presetRankCount()/presetSlotForRank()/presetRankForSlot()) rather than
// their raw, permanent slot index. Deleting a preset just clears its
// `active` flag - no data moves, no other preset's rank-facing number
// changes except by naturally shifting down past the gap.
//
// LIVE-EDIT MODEL: editing a preset's brew-process fields (via the 7-seg
// menu or the web GUI) does NOT touch the stored slot - it edits a single
// separate `workingPreset` (private to Settings.cpp, exposed only via
// activePreset()), which is what BrewStateMachine.cpp actually runs on.
// Nothing is written back to a slot until an explicit save:
// settingsSaveWorkingAsNewPreset() or settingsUpdateSlotFromWorking().
// Switching the active preset (settingsSelectPresetBySlot()) loads that
// slot's stored values into workingPreset, discarding any unsaved edits.
//
// PRESET STORAGE: the whole `preset[MAX_PRESETS]` array is written/read as
// ONE NVS blob (key "presets" in Settings.cpp), not one key per field per
// slot. A per-key scheme was tried first and silently lost newly-added
// presets: adding a preset allocates brand-new NVS entries (unlike editing
// an existing one, which just overwrites in place), and MAX_PRESETS=20 *
// ~10 keys/slot was enough entries to run the partition out of room -
// Preferences::putXxx() fails that allocation silently (no exception), so
// the new preset looked fine until the next reboot re-read from NVS and it
// was simply never there. The blob is one allocation regardless of how many
// presets are active, so this can't recur the same way.
// =====================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

struct Preset {
    bool     active;                          // false = unused slot, hidden everywhere
    char     name[PRESET_NAME_MAX_LEN + 1];   // web-set only (7-seg has no text entry)
    float    coffeeTargetTemp;
    float    steamTargetTemp;
    uint32_t preinfuseMaxMs;
    uint32_t bloomMs;
    uint32_t preheatMs;
    uint32_t brewMaxMs;
    uint32_t shotMs;            // TOTAL brew time (continuous from start) - safety cap
    float    preinfuseTargetBar;
};

struct Settings {
    // Heating PID
    double heatingKp, heatingKi, heatingKd;
    // Brew PID (Ki disabled during brew)
    double brewKp, brewKi, brewKd;
    // Global
    bool    buzzerMute;
    bool    encoderInverted;   // flips rotary CW/CCW direction
    float   coffeeTempMax;      // ERROR in COFFEE
    float   steamTempMax;       // ERROR in IDLE/STEAM + ISR cutoff
    float   safePressureMax;    // ERROR all modes
    float   ecoTargetTemp;      // PID target while in ECO
    uint32_t ecoTimeoutMs;      // continuous IDLE time before auto-entering ECO
    uint32_t sleepTimeoutMs;    // continuous IDLE time before auto-entering SLEEP (>= ecoTimeoutMs)
    uint8_t activePresetIndex;  // raw slot index (0..MAX_PRESETS-1) of the active preset
    // Preset slot pool - see the PRESET MODEL note above.
    Preset  preset[MAX_PRESETS];
};

extern Settings settings;
extern SemaphoreHandle_t settingsMutex;

#define SETTINGS_LOCK()   xSemaphoreTake(settingsMutex, portMAX_DELAY)
#define SETTINGS_UNLOCK() xSemaphoreGive(settingsMutex)

void initSettings();   // create mutex + load from NVS (call once at boot)
void loadSettings();   // NVS -> settings (DEFAULT_* fallback) + sanitize
void saveSettings();   // sanitize + settings -> NVS
void resetSettings();  // restore all DEFAULT_* + persist (single "default" preset in slot 0)

// Consistent copies for readers.
Settings settingsSnapshot();
Preset   activePreset();   // the WORKING copy - live, possibly unsaved edits (see model note above)

// Rank helpers - a "rank" is a 0-based position among currently-active
// slots, recomputed fresh on every call (cheap: at most MAX_PRESETS=20
// iterations). Used by both the 7-seg PRESET view and the web preset list
// so the two stay numbered identically.
uint8_t presetRankCount();                  // how many active presets exist (>= 1 always)
uint8_t presetSlotForRank(uint8_t rank);     // rank -> raw slot index (clamped to a valid active slot)
int8_t  presetRankForSlot(uint8_t slot);     // raw slot index -> rank, or -1 if that slot isn't active
int8_t  presetActiveRank();                  // rank of the currently active preset

// Config-layer edits (lock + clamp + persist internally).
// Caller must ensure machineState == IDLE before calling any of these -
// not enforced here (would give Settings.cpp a dependency on State.h).
void settingsApply(const Settings& incoming);   // web: store whole struct (GLOBAL fields only -
                                                 // preset fields in it are ignored, see below)
void settingsAdjustCoffeeTarget(float deltaC);   // menu: SET_COFFEE view (working copy, no persist)
void settingsAdjustPreinfuseMax(long deltaMs);   // menu: PREINFUSE view (working copy, no persist)
void settingsAdjustBloom(long deltaMs);          // menu: BLOOM view (working copy, no persist)
void settingsAdjustPreheat(long deltaMs);        // menu: PREHEAT view (working copy, no persist)
void settingsAdjustBoost(long deltaMs);          // menu: BOOST view (working copy, no persist)

// Web: stage a whole edited Preset into the working copy - no persist, same
// staged model as the 7-seg adjust functions above (clamped internally).
void settingsStageWorkingPreset(const Preset& incoming);

// Preset lifecycle - the only paths that ever touch stored slots.
void settingsSelectPresetBySlot(uint8_t slot);        // menu + web: switch active preset,
                                                       // reloads workingPreset from it
void settingsSaveWorkingAsNewPreset(const char* name); // activates the first free slot
void settingsUpdateSlotFromWorking(uint8_t slot);      // overwrites that slot's fields from
                                                        // workingPreset (keeps its name) and
                                                        // makes it the active preset
void settingsDeletePreset(uint8_t slot);               // web only - deactivates; refuses if it's
                                                        // the last active preset; if it was the
                                                        // active one, selects rank 0 instead
void settingsRenamePreset(uint8_t slot, const char* name); // web only - 7-seg has no text entry

#endif // SETTINGS_H
