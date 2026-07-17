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
    MachineState ms   = state.machineState;
    DisplayView  view = state.displayView;
    STATE_UNLOCK();

    // Always consume encoder + button edges so nothing jumps when we return to
    // IDLE, but only ACT on them in IDLE.
    long pos   = encoder.getPosition();
    long delta = pos - lastPos;
    lastPos = pos;

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

    // ECO: any encoder movement or button edge just requests a wake - no view
    // cycling, no settings edits, nothing else to input in this state.
    if (ms == STATE_ECO) {
        if (delta != 0 || shortPress || longPress) {
            STATE_LOCK();
            state.ecoWakeRequested = true;
            STATE_UNLOCK();
        }
        return;
    }

    if (ms != STATE_IDLE) return;          // menu locked outside IDLE (edges consumed)

    // Long press in SET view: toggle the edit granularity (whole degrees <-> tenths).
    // Every other view has no dedicated long-press action - fall back to the
    // short-press behavior (cycle view) rather than leaving the hold a dead end.
    if (longPress) {
        if (view == VIEW_SET_COFFEE) {
            STATE_LOCK();
            state.setEditDecimals = !state.setEditDecimals;
            STATE_UNLOCK();
            buzzerPlay(SND_CLICK);
        } else {
            DisplayView next = (DisplayView)((view + 1) % DISPLAY_VIEW_COUNT);
            STATE_LOCK();
            state.displayView = next;
            STATE_UNLOCK();
            buzzerPlay(SND_CLICK);
        }
        return;
    }

    // Short press: cycle the view.
    if (shortPress) {
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
                STATE_LOCK();
                bool dec = state.setEditDecimals;
                STATE_UNLOCK();
                float step = dec ? COFFEE_TEMP_STEP_FINE : COFFEE_TEMP_STEP_WHOLE;
                settingsAdjustCoffeeTarget((float)delta * step);
                buzzerPlay(SND_TICK);
                break;
            }
            case VIEW_TIMER:
                settingsAdjustShotTime((long)delta * SHOT_TIME_STEP_MS);
                buzzerPlay(SND_TICK);
                break;
            case VIEW_PRESET: {
                SETTINGS_LOCK();
                uint8_t idx = settings.activePresetIndex;
                SETTINGS_UNLOCK();
                long ni = ((long)idx + delta) % NUM_PRESETS;
                if (ni < 0) ni += NUM_PRESETS;
                settingsSetActivePreset((uint8_t)ni);
                buzzerPlay(SND_TICK);
                break;
            }
            default: break;   // VIEW_TEMP, VIEW_IP: no edit
        }
    }
}
