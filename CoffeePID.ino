/*
 * CoffeePID.ino
 * Main Entry Point
 * 
 * Architecture:
 * - Core 0: Display Refresh + Web Server
 * - Core 1: Control Loop (10Hz, Logic)
 * - Interrupts: Rotary Encoder & Button (Instant)
 */

#include "State.h"
#include "Sensors.h"
#include "Input.h"
#include "Display.h"
#include "Controls.h"
#include "StateMachine.h"
#include "WebServer.h"
#include "Buzzer.h"

// --- RTOS TASK HANDLES ---
TaskHandle_t TaskDisplayHandle;
TaskHandle_t TaskControlHandle;
TaskHandle_t TaskWebServerHandle;

// --- TASK: DISPLAY (CORE 0) ---
// Keeps the 7-segment display alive without flickering.
void TaskDisplay(void * pvParameters) {
  setupDisplay(); // Initialize display hardware

  for(;;) {
    refreshDisplay(); 
    vTaskDelay(TASK_DISPLAY_CYCLE_MS / portTICK_PERIOD_MS);
  }
}

// --- TASK: CONTROL (CORE 1) ---
// Handles Sensor -> PID -> Relay logic @ 10Hz.
void TaskControl(void * pvParameters) {
  setupSensors();         // Initialize TSIC + pressure pin attenuation
  setupControls();        // Initialize heater relay, pump, valve, PID, hardware timer
  setupStateMachine();    // Set safe output defaults, log initial state

  const TickType_t xFrequency = TASK_CONTROL_CYCLE_MS / portTICK_PERIOD_MS;
  TickType_t xLastWakeTime = xTaskGetTickCount();

  for(;;) {
    // 1. Read Inputs (Rotary is Interrupt-based, but we sync state here)
    syncInputState();

    // 2. Read Sensors
    readTemperature();
    readPressure();

    // 3. Run PID computation (always runs; state machine controls which output the ISR uses)
    updatePID();

    // 4. Evaluate state machine transitions and apply hardware outputs
    updateStateMachine();

    // 5. Wait until the next 100ms cycle (Accurate timing)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// --- TASK: WEB SERVER (CORE 0) ---
// Simple HTTP server for debugging
void TaskWebServer(void * pvParameters) {
  Serial.printf("[WEB] Task started on core %d, stack=%u free\n",
    xPortGetCoreID(), uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
  setupWebServer();  // Initialize WiFi AP and web server
  Serial.println("[WEB] Entering request loop");

  for(;;) {
    handleWebServer();  // Process HTTP requests
    vTaskDelay(TASK_WEBSERVER_CYCLE_MS / portTICK_PERIOD_MS);
  }
}

// --- MAIN SETUP ---
void setup() {
  Serial.begin(115200);

  // Initialize buzzer and play jingle during the startup window
  setupBuzzer();
  playStartupJingle();

  delay(STARTUP_DELAY_MS);
  Serial.println("\n\n========================================");
  Serial.println("--- SYSTEM STARTING ---");
  Serial.printf("[BOOT] Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("[BOOT] CPU freq: %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("[BOOT] SDK version: %s\n", ESP.getSdkVersion());

  // 0. Initialize state mutex FIRST (before any tasks access shared state)
  Serial.println("[BOOT] Step 0: initStateMutex...");
  initStateMutex();
  Serial.println("[BOOT] Step 0: DONE");

  // 1. Load saved PID settings from NVS (before any tasks start)
  Serial.println("[BOOT] Step 1: loadPIDFromStorage...");
  loadPIDFromStorage();
  Serial.println("[BOOT] Step 1: DONE");

  // 2. Setup Inputs (Interrupts attach here)
  Serial.println("[BOOT] Step 2: setupInput...");
  setupInput();
  Serial.println("[BOOT] Step 2: DONE");

  // 3. Create Display Task on CORE 0
  Serial.println("[BOOT] Step 3: creating Display task...");
  BaseType_t dispResult = xTaskCreatePinnedToCore(
    TaskDisplay, "Display", TASK_DISPLAY_STACK, NULL, TASK_DISPLAY_PRIORITY, &TaskDisplayHandle, 0
  );
  Serial.printf("[BOOT] Step 3: Display task %s (handle=%p)\n",
    dispResult == pdPASS ? "CREATED OK" : "FAILED", (void*)TaskDisplayHandle);

  // 4. Create Web Server Task on CORE 0
  Serial.println("[BOOT] Step 4: creating WebServer task...");
  BaseType_t wsResult = xTaskCreatePinnedToCore(
    TaskWebServer, "WebServer", TASK_WEBSERVER_STACK, NULL, TASK_WEBSERVER_PRIORITY, &TaskWebServerHandle, 0
  );
  Serial.printf("[BOOT] Step 4: WebServer task %s (handle=%p)\n",
    wsResult == pdPASS ? "CREATED OK" : "FAILED", (void*)TaskWebServerHandle);

  // 5. Create Control Task on CORE 1 (highest priority)
  Serial.println("[BOOT] Step 5: creating Control task...");
  BaseType_t ctrlResult = xTaskCreatePinnedToCore(
    TaskControl, "Control", TASK_CONTROL_STACK, NULL, TASK_CONTROL_PRIORITY, &TaskControlHandle, 1
  );
  Serial.printf("[BOOT] Step 5: Control task %s (handle=%p)\n",
    ctrlResult == pdPASS ? "CREATED OK" : "FAILED", (void*)TaskControlHandle);

  Serial.printf("[BOOT] Setup complete. Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println("========================================");
}

// --- MAIN LOOP ---
void loop() {
  // Empty - we use RTOS Tasks above
  vTaskDelete(NULL);
}
