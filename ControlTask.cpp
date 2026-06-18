#include "ControlTask.h"
#include "Config.h"
#include "State.h"
#include "Settings.h"
#include "Switches.h"
#include "Temperature.h"
#include "Pressure.h"
#include "BrewStateMachine.h"
#include "Pid.h"
#include "Heater.h"

static void controlTask(void* pv) {
    // Init all control-side hardware/modules on this core.
    setupSwitches();
    setupTemperature();
    setupPressure();
    setupBrew();
    setupPid();
    setupHeater();
    Serial.println("[CONTROL] task running on core 1");

    const TickType_t period = pdMS_TO_TICKS(TASK_CONTROL_CYCLE_MS);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        // 1-3: sense
        readSwitches();
        readTemperature();
        readPressure();
        // 4: decide (sets heaterMode, target, pump, valve, ...)
        updateBrewStateMachine();
        // 5: control math (uses the fresh target + gain set)
        computePid();

        // Publish the decision to the heater ISR (no locks in the ISR).
        STATE_LOCK();
        HeaterMode hm   = state.heaterMode;
        uint32_t   duty = (uint32_t)state.heatingPidOutput;
        float      temp = state.currentTemperature;
        STATE_UNLOCK();
        SETTINGS_LOCK();
        float cutoff = settings.steamTempMax;   // absolute ISR backstop
        SETTINGS_UNLOCK();
        heaterUpdate(hm, duty, temp, cutoff);

        vTaskDelayUntil(&last, period);
    }
}

void startControlTask() {
    xTaskCreatePinnedToCore(controlTask, "Control", TASK_CONTROL_STACK, NULL,
                            TASK_CONTROL_PRIORITY, NULL, TASK_CONTROL_CORE);
}
