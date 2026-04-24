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
#include "WebServer.h"

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
  setupSensors();  // Initialize TSIC
  setupControls(); // Initialize Relay/PID

  const TickType_t xFrequency = TASK_CONTROL_CYCLE_MS / portTICK_PERIOD_MS;
  TickType_t xLastWakeTime = xTaskGetTickCount();

  for(;;) {
    // 1. Read Inputs (Rotary is Interrupt-based, but we sync state here)
    syncInputState(); 

    // 2. Read Temperature
    readTemperature(); 

    // 3. Calculate PID & Update Relay
    updatePID();
    
    // 4. Wait until the next 100ms cycle (Accurate timing)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// --- TASK: WEB SERVER (CORE 0) ---
// Simple HTTP server for debugging
void TaskWebServer(void * pvParameters) {
  setupWebServer();  // Initialize WiFi AP and web server

  for(;;) {
    handleWebServer();  // Process HTTP requests
    vTaskDelay(TASK_WEBSERVER_CYCLE_MS / portTICK_PERIOD_MS);
  }
}

// --- MAIN SETUP ---
void setup() {
  Serial.begin(115200);

  delay(STARTUP_DELAY_MS);
  Serial.println("--- SYSTEM STARTING ---");

  // 0. Initialize state mutex FIRST (before any tasks access shared state)
  initStateMutex();

  // 1. Load saved PID settings from NVS (before any tasks start)
  loadPIDFromStorage();

  // 2. Setup Inputs (Interrupts attach here)
  setupInput();

  // 3. Create Display Task on CORE 0
  xTaskCreatePinnedToCore(
    TaskDisplay, "Display", TASK_DISPLAY_STACK, NULL, TASK_DISPLAY_PRIORITY, &TaskDisplayHandle, 0
  );

  // 4. Create Web Server Task on CORE 0
  xTaskCreatePinnedToCore(
    TaskWebServer, "WebServer", TASK_WEBSERVER_STACK, NULL, TASK_WEBSERVER_PRIORITY, &TaskWebServerHandle, 0
  );

  // 5. Create Control Task on CORE 1 (highest priority)
  xTaskCreatePinnedToCore(
    TaskControl, "Control", TASK_CONTROL_STACK, NULL, TASK_CONTROL_PRIORITY, &TaskControlHandle, 1
  );
}

// --- MAIN LOOP ---
void loop() {
  // Empty - we use RTOS Tasks above
  vTaskDelete(NULL);
}
