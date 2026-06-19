#include "Buzzer.h"
#include "Settings.h"
#include "Config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct Step { uint16_t freq; uint16_t durMs; };

static QueueHandle_t soundQueue = NULL;

// Active one-shot sequence
static Step     seq[4];
static uint8_t  seqLen = 0, seqIdx = 0;
static bool     seqActive = false;
static uint32_t stepStartMs = 0;

// Error siren
static uint32_t sirenLastMs = 0;
static bool     sirenHi   = false;
static bool     sirenOn   = false;

static bool isMuted() {
    SETTINGS_LOCK();
    bool m = settings.buzzerMute;
    SETTINGS_UNLOCK();
    return m;
}

static void startStep(uint32_t now) {
    if (!isMuted() && seq[seqIdx].freq > 0)
        tone(PIN_BUZZER, seq[seqIdx].freq, seq[seqIdx].durMs);
    stepStartMs = now;
}

static void buildSequence(Sound s) {
    switch (s) {
        case SND_TICK:
            seq[0] = { TONE_TICK_HZ,  TONE_CLICK_MS }; seqLen = 1; break;
        case SND_CLICK:
            seq[0] = { TONE_CLICK_HZ, TONE_CLICK_MS }; seqLen = 1; break;
        case SND_MODE_ENTER:   // ascending perfect fifth
            seq[0] = { TONE_FIFTH_LOW_HZ,  TONE_NOTE_MS };
            seq[1] = { TONE_FIFTH_HIGH_HZ, TONE_NOTE_MS }; seqLen = 2; break;
        case SND_MODE_EXIT:    // descending
            seq[0] = { TONE_FIFTH_HIGH_HZ, TONE_NOTE_MS };
            seq[1] = { TONE_FIFTH_LOW_HZ,  TONE_NOTE_MS }; seqLen = 2; break;
        default: seqLen = 0; break;
    }
}

void buzzerInit() {
    soundQueue = xQueueCreate(8, sizeof(Sound));
}

void buzzerStartupJingle() {
    if (isMuted()) return;
    // Drive the LEDC peripheral directly rather than tone(): at boot (before the
    // UI task starts) tone()'s lazily-created background task can swallow the very
    // first notes, which left the startup jingle silent. A direct attach/writeTone
    // is deterministic. Detach afterwards so tone() can re-own the pin at runtime.
    const uint16_t notes[] = { TONE_FIFTH_LOW_HZ, TONE_FIFTH_HIGH_HZ, TONE_FIFTH_HIGH_HZ * 2 };
    ledcAttach(PIN_BUZZER, TONE_FIFTH_LOW_HZ, 10);
    for (uint8_t i = 0; i < 3; i++) {
        ledcWriteTone(PIN_BUZZER, notes[i]);
        delay(TONE_NOTE_MS);
    }
    ledcWriteTone(PIN_BUZZER, 0);   // silence
    ledcDetach(PIN_BUZZER);
}

void buzzerPlay(Sound s) {
    if (soundQueue) xQueueSend(soundQueue, &s, 0);   // non-blocking, drop if full
}

void buzzerTick(bool errorSiren) {
    uint32_t now = millis();

    // 1. A running one-shot sequence has priority.
    if (seqActive) {
        if (now - stepStartMs >= seq[seqIdx].durMs) {
            if (++seqIdx >= seqLen) { seqActive = false; noTone(PIN_BUZZER); }
            else startStep(now);
        }
        return;
    }

    // 2. Error siren (level-driven, alternating two-tone).
    if (errorSiren) {
        if (now - sirenLastMs >= TONE_SIREN_MS) {
            sirenHi = !sirenHi;
            if (!isMuted())
                tone(PIN_BUZZER, sirenHi ? TONE_SIREN_HI_HZ : TONE_SIREN_LO_HZ, TONE_SIREN_MS);
            sirenLastMs = now;
        }
        sirenOn = true;
        return;
    }
    if (sirenOn) { noTone(PIN_BUZZER); sirenOn = false; }

    // 3. Start the next queued one-shot.
    Sound s;
    if (soundQueue && xQueueReceive(soundQueue, &s, 0) == pdTRUE) {
        buildSequence(s);
        if (seqLen > 0) { seqActive = true; seqIdx = 0; startStep(now); }
    }
}
