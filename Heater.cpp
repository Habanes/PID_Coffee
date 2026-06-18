#include "Heater.h"
#include "Config.h"
#include <Arduino.h>

// SSR is active HIGH. Pump/valve elsewhere are active LOW.
static hw_timer_t* heaterTimer = NULL;

// Volatiles shared with the ISR. Scalars are 32-bit -> atomic R/W on Xtensa.
static volatile uint8_t  g_mode    = HEATER_OFF;   // HeaterMode
static volatile uint32_t g_dutyMs  = 0;            // ON time within the window (PID mode)
static volatile bool     g_cutoff  = false;        // hard over-temp cutoff
static volatile uint32_t g_counter = 0;            // window position (ms)

// 100 Hz: time-proportional SSR control over SSR_WINDOW_MS.
void IRAM_ATTR onHeaterTimer() {
    if (g_cutoff || g_mode == HEATER_OFF) {
        digitalWrite(PIN_HEATER_SSR, LOW);
    } else if (g_mode == HEATER_FULL_ON) {
        digitalWrite(PIN_HEATER_SSR, HIGH);
    } else { // HEATER_PID
        digitalWrite(PIN_HEATER_SSR, (g_counter < g_dutyMs) ? HIGH : LOW);
    }
    g_counter += HEATER_TIMER_INTERVAL_MS;
    if (g_counter >= SSR_WINDOW_MS) g_counter = 0;
}

void setupHeater() {
    pinMode(PIN_HEATER_SSR, OUTPUT);
    digitalWrite(PIN_HEATER_SSR, LOW);

    heaterTimer = timerBegin(1000000);                          // 1 MHz base
    timerAttachInterrupt(heaterTimer, &onHeaterTimer);
    timerAlarm(heaterTimer, HEATER_TIMER_INTERVAL_MS * 1000UL,  // every 10 ms
               true, 0);                                        // autoreload
}

void heaterUpdate(HeaterMode mode, uint32_t dutyMs, float currentTemp, float cutoffTemp) {
    g_cutoff = (currentTemp > cutoffTemp);   // fail-safe, independent of mode
    g_dutyMs = dutyMs;
    g_mode   = (uint8_t)mode;
}
