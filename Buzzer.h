#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include "Config.h"

// =====================================================================
// NOTE FREQUENCIES (Hz)
// =====================================================================
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define REST     0

// =====================================================================
// API
// =====================================================================

// Runtime mute control (persisted via NVS — loaded by loadPIDFromStorage)
bool getBuzzerMute();
void setBuzzerMute(bool muted);

// Call once before RTOS tasks start (uses delay() internally — blocking)
void setupBuzzer();
void playStartupJingle();

// Non-blocking one-shot sounds (fire-and-forget via tone() duration)
void playButtonClick();    // Short press feedback
void playLongPress();      // Long press sensitivity toggle confirmed
void playEncoderTick();    // Encoder step in SET mode
void playBrewToggle();     // Brew mode on/off

// Non-blocking siren: call repeatedly from display/control loop
// Pass isEmergencyStopActive() as argument
void updateSiren(bool active);

#endif // BUZZER_H
