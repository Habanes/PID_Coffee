#ifndef BUZZER_H
#define BUZZER_H
#include <Arduino.h>

#define BUZZER_PIN 47

void setupBuzzer();
void buzzerStartupJingle();  // Played once at boot
void buzzerIdleJingle();     // Frequency sweep test - call from a task loop
void buzzerRotaryTick();     // Short click on rotation
void buzzerButtonPress();    // Short press confirmation
void buzzerLongPress();      // Long press / mode change

#endif
