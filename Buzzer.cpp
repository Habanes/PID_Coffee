#include "Buzzer.h"

// Runtime mute flag — default from compile-time BUZZER_MUTE, overridden by NVS at boot
static bool _buzzerMuted = BUZZER_MUTE;

bool getBuzzerMute() { return _buzzerMuted; }
void setBuzzerMute(bool muted) {
    _buzzerMuted = muted;
    if (muted) noTone(BUZZER_PIN);  // Kill any tone currently playing
}

// =====================================================================
// STARTUP JINGLE  — "ee-abc / cc-baf"  (120 BPM)
// =====================================================================
static const int _melody[] = {
    NOTE_E4, NOTE_E4, NOTE_A4, NOTE_B4, NOTE_C5,
    REST,
    NOTE_C5, NOTE_C5, NOTE_B4, NOTE_A4, NOTE_F4
};
static const int _beats[] = {
    1, 1, 1, 1, 4,
    1,
    1, 1, 1, 1, 4
};
static const int _noteCount = sizeof(_melody) / sizeof(_melody[0]);

// =====================================================================
// SETUP
// =====================================================================
void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
}

// =====================================================================
// STARTUP JINGLE  (blocking — call before tasks start)
// =====================================================================
void playStartupJingle() {
    if (_buzzerMuted) return;
    // 120 BPM → 500 ms per beat; tripled = 360 BPM → ~167 ms per beat
    const int msPerBeat = 500 / 3;

    for (int i = 0; i < _noteCount; i++) {
        int duration = _beats[i] * msPerBeat;

        if (_melody[i] == REST) {
            noTone(BUZZER_PIN);
        } else {
            tone(BUZZER_PIN, _melody[i], duration);
        }

        // 10% gap between notes so repeated pitches don't blur together
        delay((int)(duration * 1.10f));
        noTone(BUZZER_PIN);
    }
}

// =====================================================================
// ONE-SHOT NON-BLOCKING SOUNDS
// =====================================================================

// Short press / generic button acknowledgment
void playButtonClick() {
    if (_buzzerMuted) return;
    tone(BUZZER_PIN, 1000, 50);
}

// Long press — sensitivity toggle confirmed (lower, longer = "heavier" feel)
void playLongPress() {
    if (_buzzerMuted) return;
    tone(BUZZER_PIN, 700, 180);
}

// Encoder step while in SET mode (very short, high-pitched tick)
void playEncoderTick() {
    if (_buzzerMuted) return;
    tone(BUZZER_PIN, 2000, 12);
}

// Brew mode toggled on or off
void playBrewToggle() {
    if (_buzzerMuted) return;
    tone(BUZZER_PIN, 600, 220);
}

// =====================================================================
// SIREN  (call repeatedly from display/control loop — non-blocking)
// =====================================================================
void updateSiren(bool active) {
    static bool wasActive       = false;
    static unsigned long lastToggleMs = 0;
    static bool sirenHigh       = false;

    if (!active) {
        if (wasActive) {
            noTone(BUZZER_PIN);
            wasActive = false;
        }
        return;
    }

    if (_buzzerMuted) {
        wasActive = false;  // Reset so cleanup path re-arms correctly when unmuted
        return;
    }
    wasActive = true;
    unsigned long now = millis();
    if (now - lastToggleMs >= 300) {
        lastToggleMs = now;
        sirenHigh = !sirenHigh;
        // Alternate between a high wail and a low growl
        tone(BUZZER_PIN, sirenHigh ? 1400 : 800);
    }
}
