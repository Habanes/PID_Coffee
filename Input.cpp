#include "Input.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include "Buzzer.h"
#include <RotaryEncoder.h>

// A/B intentionally swapped (PIN_ENC_B first) to flip CW/CCW direction.
static RotaryEncoder encoder(PIN_ENC_B, PIN_ENC_A, RotaryEncoder::LatchMode::TWO03);
static void IRAM_ATTR encoderISR() { encoder.tick(); }

static long     lastPos   = 0;
static bool     lastBtn   = HIGH;
static uint32_t lastBtnMs = 0;
static bool     btnDown     = false;   // currently held (debounced)
static uint32_t btnDownMs   = 0;       // when the press started
static bool     longHandled = false;   // long action already fired this hold

void setupInput() {
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);
    pinMode(PIN_BTN, INPUT_PULLUP);   // button to GND
    lastPos = encoder.getPosition();
}

void syncInput() {
    // Read current view + mode.
    STATE_LOCK();
    MachineState ms       = state.machineState;
    DisplayView  view     = state.displayView;
    bool         saveMode = state.presetSaveMode;
    uint8_t      saveTgt  = state.presetSaveTargetRank;
    STATE_UNLOCK();

    // Always consume encoder + button edges so nothing jumps when we return to
    // IDLE, but only ACT on them in IDLE.
    long pos   = encoder.getPosition();
    long delta = pos - lastPos;
    lastPos = pos;

    SETTINGS_LOCK();
    bool inverted = settings.encoderInverted;
    SETTINGS_UNLOCK();
    if (inverted) delta = -delta;

    uint32_t now = millis();

    // Debounced button edges. Short press acts on RELEASE (so a hold that crosses
    // the long-press threshold does not also cycle the view); long press fires
    // while still held.
    bool shortPress = false, longPress = false;
    bool btn = digitalRead(PIN_BTN);           // LOW = pressed
    if (btn != lastBtn && (now - lastBtnMs) > BTN_DEBOUNCE_MS) {
        lastBtnMs = now;
        lastBtn   = btn;
        if (btn == LOW) {                      // falling edge = press
            btnDown = true; btnDownMs = now; longHandled = false;
        } else {                               // rising edge = release
            if (btnDown && !longHandled) shortPress = true;
            btnDown = false;
        }
    }
    if (btnDown && !longHandled && (now - btnDownMs) >= BTN_LONG_PRESS_MS) {
        longHandled = true;
        longPress   = true;
    }

    // ECO/SLEEP: any encoder movement or button edge just requests a wake - no
    // view cycling, no settings edits, nothing else to input in either state.
    if (ms == STATE_ECO || ms == STATE_SLEEP) {
        if (delta != 0 || shortPress || longPress) {
            STATE_LOCK();
            state.wakeRequested = true;
            STATE_UNLOCK();
        }
        return;
    }

    if (ms != STATE_IDLE) return;          // menu locked outside IDLE (edges consumed)

    // Preset override/save screen (entered from VIEW_PRESET, see below) - a
    // nested mode, not a DisplayView of its own, since it needs the rotary
    // scrolling a completely different axis (save target, not menu view).
    if (saveMode) {
        uint8_t rankCount = presetRankCount();   // existing active presets...
        uint8_t maxTarget = rankCount;           // ...+1 virtual "new" slot at this index
        if (longPress) {
            // Cancel back to the normal preset view - no save. Distinct from
            // long-press's OWN entry gesture below (same button, different
            // meaning depending on which screen is already showing).
            STATE_LOCK(); state.presetSaveMode = false; STATE_UNLOCK();
            buzzerPlay(SND_CLICK);
            return;
        }
        if (shortPress) {
            if (saveTgt >= rankCount) {
                // Default name distinguishes new presets from the 7-seg (no
                // text entry here) - rename anytime from the web GUI.
                char nameBuf[PRESET_NAME_MAX_LEN + 1];
                snprintf(nameBuf, sizeof(nameBuf), "Preset %u", (unsigned)(rankCount + 1));
                settingsSaveWorkingAsNewPreset(nameBuf);
            } else {
                settingsUpdateSlotFromWorking(presetSlotForRank(saveTgt));
            }
            STATE_LOCK(); state.presetSaveMode = false; STATE_UNLOCK();
            buzzerPlay(SND_CLICK);
            return;
        }
        if (delta != 0) {
            long wrap = (long)maxTarget + 1;   // ranks 0..rankCount-1, plus "new" at rankCount
            long nt = ((long)saveTgt + delta) % wrap;
            if (nt < 0) nt += wrap;
            STATE_LOCK(); state.presetSaveTargetRank = (uint8_t)nt; STATE_UNLOCK();
            buzzerPlay(SND_TICK);
        }
        return;
    }

    // Long-press from the PRESET view enters the override/save screen above,
    // defaulting the highlighted target to whichever preset is active now.
    if (longPress && view == VIEW_PRESET) {
        int8_t rank = presetActiveRank();
        STATE_LOCK();
        state.presetSaveMode       = true;
        state.presetSaveTargetRank = (rank >= 0) ? (uint8_t)rank : 0;
        STATE_UNLOCK();
        buzzerPlay(SND_CLICK);
        return;
    }

    // No other view has a dedicated long-press action (the old SET_COFFEE
    // decimal-granularity toggle was retired along with decimal display/edit
    // entirely) - long press just falls back to the same cycle-view behavior
    // as short press. longPress/shortPress stay separately detected above so
    // a future long-press action is a one-line branch-split, not a rebuild.
    if (longPress || shortPress) {
        DisplayView next = (DisplayView)((view + 1) % DISPLAY_VIEW_COUNT);
        STATE_LOCK();
        state.displayView = next;
        STATE_UNLOCK();
        buzzerPlay(SND_CLICK);
        return;                            // one action per cycle
    }

    // Encoder: edit within the current view.
    if (delta != 0) {
        switch (view) {
            case VIEW_SET_COFFEE: {
                settingsAdjustCoffeeTarget((float)delta * COFFEE_TEMP_STEP_WHOLE);
                buzzerPlay(SND_TICK);
                break;
            }
            case VIEW_PREINFUSE:
                settingsAdjustPreinfuseMax((long)delta * PREINFUSE_STEP_MS);
                buzzerPlay(SND_TICK);
                break;
            case VIEW_BLOOM:
                settingsAdjustBloom((long)delta * BLOOM_STEP_MS);
                buzzerPlay(SND_TICK);
                break;
            case VIEW_PREHEAT:
                settingsAdjustPreheat((long)delta * PREHEAT_STEP_MS);
                buzzerPlay(SND_TICK);
                break;
            case VIEW_BOOST:
                settingsAdjustBoost((long)delta * BOOST_STEP_MS);
                buzzerPlay(SND_TICK);
                break;
            case VIEW_PRESET: {
                // Browse by rank among active presets, not raw slot index -
                // see Settings.h's PRESET MODEL note.
                uint8_t rankCount = presetRankCount();
                int8_t  curRank   = presetActiveRank();
                if (rankCount > 0 && curRank >= 0) {
                    long nr = ((long)curRank + delta) % rankCount;
                    if (nr < 0) nr += rankCount;
                    settingsSelectPresetBySlot(presetSlotForRank((uint8_t)nr));
                }
                buzzerPlay(SND_TICK);
                break;
            }
            default: break;   // VIEW_TEMP, VIEW_IP: no edit
        }
    }
}
