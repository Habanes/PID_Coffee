#include "Buzzer.h"

// LEDC resolution: 8-bit (duty 0-255, 128 = 50% = loudest for passive buzzer)
#define BUZZER_RESOLUTION_BITS 8
#define BUZZER_DUTY_ON  128  // 50% duty cycle
#define BUZZER_DUTY_OFF   0

// --- Internal helpers ---

static void buzzerOn(uint32_t freq) {
    ledcWriteTone(BUZZER_PIN, freq);  // Sets frequency; duty stays at last value
    ledcWrite(BUZZER_PIN, BUZZER_DUTY_ON);
}

static void buzzerOff() {
    ledcWrite(BUZZER_PIN, BUZZER_DUTY_OFF);
}

// --- Public API ---

void setupBuzzer() {
    // Attach LEDC to the buzzer pin (ESP32 Arduino Core 3.x API)
    ledcAttach(BUZZER_PIN, 2000, BUZZER_RESOLUTION_BITS);
    buzzerOff();
    Serial.println("[BUZZER] LEDC initialized on GPIO" + String(BUZZER_PIN));
}

/**
 * @brief Startup jingle - G major arpeggio (G4 B4 D5 G5)
 * Blocking - safe to call from setup() before tasks start.
 */
void buzzerStartupJingle() {
    const uint32_t notes[]    = { 392, 494, 587, 784 }; // G4, B4, D5, G5
    const uint32_t durations[] = { 120, 120, 120, 250 };
    const uint32_t gaps[]      = {  30,  30,  30,   0 };

    for (int i = 0; i < 4; i++) {
        buzzerOn(notes[i]);
        delay(durations[i]);
        buzzerOff();
        if (gaps[i] > 0) delay(gaps[i]);
    }
}

/**
 * @brief Idle test jingle - sweeps through frequencies so you can verify
 * the buzzer works and hear which range it responds to.
 * Call this from a dedicated RTOS task (see CoffeePID.ino).
 * Each note is played for 200ms with a 50ms gap, then 1s silence, repeat.
 */
void buzzerIdleJingle() {
    // Covers a wide range: 200Hz -> 16kHz
    // Passive buzzers typically peak around 2-4kHz
    const uint32_t freqs[] = { 200, 400, 800, 1000, 1500, 2000, 3000, 4000, 6000, 8000 };
    const int count = sizeof(freqs) / sizeof(freqs[0]);

    for (int i = 0; i < count; i++) {
        Serial.printf("[BUZZER TEST] %u Hz\n", freqs[i]);
        buzzerOn(freqs[i]);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        buzzerOff();
        vTaskDelay(80 / portTICK_PERIOD_MS);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1s pause before repeating
}

/**
 * @brief Very short high tick on rotary rotation.
 */
void buzzerRotaryTick() {
    buzzerOn(2000);
    vTaskDelay(15 / portTICK_PERIOD_MS);
    buzzerOff();
}

/**
 * @brief Mid-pitch beep on short button press.
 */
void buzzerButtonPress() {
    buzzerOn(1500);
    vTaskDelay(60 / portTICK_PERIOD_MS);
    buzzerOff();
}

/**
 * @brief Lower double-beep on long press / mode change.
 */
void buzzerLongPress() {
    buzzerOn(800);
    vTaskDelay(80 / portTICK_PERIOD_MS);
    buzzerOff();
    vTaskDelay(60 / portTICK_PERIOD_MS);
    buzzerOn(800);
    vTaskDelay(80 / portTICK_PERIOD_MS);
    buzzerOff();
}
