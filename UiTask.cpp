#include "UiTask.h"
#include "Config.h"
#include "State.h"
#include "Display.h"
#include "Input.h"
#include "Buzzer.h"

static void uiTask(void* pv) {
    setupDisplay();
    setupInput();
    Serial.println("[UI] task running on core 0");

    const TickType_t period = pdMS_TO_TICKS(TASK_UI_CYCLE_MS);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        refreshDisplay();   // fast multiplex - must run every cycle
        syncInput();        // encoder/button (IDLE-only actions)

        STATE_LOCK();
        bool errorSiren = (state.machineState == STATE_ERROR);
        STATE_UNLOCK();
        buzzerTick(errorSiren);

        vTaskDelayUntil(&last, period);
    }
}

void startUiTask() {
    xTaskCreatePinnedToCore(uiTask, "Ui", TASK_UI_STACK, NULL,
                            TASK_UI_PRIORITY, NULL, TASK_UI_CORE);
}
