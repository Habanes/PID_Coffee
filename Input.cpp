#include "Input.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include "Buzzer.h"
#include <RotaryEncoder.h>

static RotaryEncoder encoder(PIN_ENC_A, PIN_ENC_B, RotaryEncoder::LatchMode::TWO03);
static void IRAM_ATTR encoderISR() { encoder.tick(); }

static long     lastPos   = 0;
static bool     lastBtn   = HIGH;
static uint32_t lastBtnMs = 0;

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

    bool btn = digitalRead(PIN_BTN);
    bool pressed = false;
    if (btn != lastBtn && (millis() - lastBtnMs) > BTN_DEBOUNCE_MS) {
        lastBtnMs = millis();
        lastBtn   = btn;
        if (btn == LOW) pressed = true;   // falling edge = press
    }

    if (ms != STATE_IDLE) return;          // menu locked outside IDLE

    // Button: cycle the view.
    if (pressed) {
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
            case VIEW_SET_COFFEE:
                settingsAdjustCoffeeTarget((float)delta * COFFEE_TEMP_STEP);
                buzzerPlay(SND_TICK);
                break;
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
