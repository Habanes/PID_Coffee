/*
 * CoffeePID - QuickMill Orione 3000 PID controller (clean rebuild).
 *
 * Cores:  1 = real-time control (ControlTask)
 *         0 = UI (UiTask) + Web (WebTask)
 * ISRs:   heater SSR (100 Hz hardware timer), rotary encoder (pin-change)
 *
 * See ../Architecture.txt, ../Processes.txt, ../FreeRTOS Management.txt.
 */

#include "Config.h"
#include "State.h"
#include "Settings.h"
#include "Buzzer.h"
#include "ControlTask.h"
#include "UiTask.h"
#include "Web.h"

void setup() {
    Serial.begin(115200);

    // Shared foundations first (mutexes must exist before any task starts).
    initState();
    initSettings();      // creates settingsMutex + loads NVS (defaults fallback)
    buzzerInit();        // sound queue

    buzzerStartupJingle();          // boot beep (blocking, before tasks)
    delay(STARTUP_DELAY_MS);
    Serial.println("\n=== CoffeePID starting ===");

    // Tasks: control on core 1, UI + web on core 0.
    startControlTask();
    startUiTask();
    startWebTask();

    Serial.printf("[BOOT] complete, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
    vTaskDelete(NULL);   // everything runs in tasks
}
