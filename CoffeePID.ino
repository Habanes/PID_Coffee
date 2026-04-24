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
#include "Buzzer.h"

// --- RTOS TASK HANDLES ---
TaskHandle_t TaskDisplayHandle;
TaskHandle_t TaskControlHandle;
TaskHandle_t TaskWebServerHandle;
TaskHandle_t TaskBuzzerTestHandle;

// --- TASK: DISPLAY (CORE 0) ---
// Keeps the 7-segment display alive without flickering.
void TaskDisplay(void * pvParameters) {
  setupDisplay(); // Initialize display hardware

  for(;;) {
    refreshDisplay(); 
    vTaskDelay(2 / portTICK_PERIOD_MS); // 2ms cycle
  }
}

// --- TASK: CONTROL (CORE 1) ---
// Handles Sensor -> PID -> Relay logic @ 10Hz.
void TaskControl(void * pvParameters) {
  setupSensors();  // Initialize TSIC
  setupControls(); // Initialize Relay/PID

  const TickType_t xFrequency = 100 / portTICK_PERIOD_MS; // 100ms = 10Hz
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
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Small delay
  }
}

// --- TASK: BUZZER TEST (CORE 0) ---
// Continuously sweeps through frequencies so the buzzer can be verified.
// Remove or comment out once the buzzer is confirmed working.
void TaskBuzzerTest(void * pvParameters) {
  for(;;) {
    buzzerIdleJingle(); // Sweeps 200Hz-8kHz, logs each frequency to Serial
  }
}

// --- MAIN SETUP ---
void setup() {
  Serial.begin(115200);

  delay(2000); 
  Serial.println("--- SYSTEM STARTING ---");

  // 0. Initialize state mutex FIRST (before any tasks access shared state)
  initStateMutex();

  // 1. Load saved PID settings from NVS (before any tasks start)
  loadPIDFromStorage();

  // 2. Setup Inputs (Interrupts attach here)
  setupInput();

  // 3. Setup Buzzer and play startup jingle
  setupBuzzer();
  buzzerStartupJingle();

  // 4. Create Display Task on CORE 0
  xTaskCreatePinnedToCore(
    TaskDisplay, "Display", 4096, NULL, 1, &TaskDisplayHandle, 0
  );

  // 5. Create Web Server Task on CORE 0
  xTaskCreatePinnedToCore(
    TaskWebServer, "WebServer", 8192, NULL, 1, &TaskWebServerHandle, 0
  );

  // 6. Create Control Task on CORE 1 (highest priority)
  xTaskCreatePinnedToCore(
    TaskControl, "Control", 4096, NULL, 2, &TaskControlHandle, 1
  );

  // 7. Buzzer test task disabled (buzzer verified)
  // xTaskCreatePinnedToCore(
  //   TaskBuzzerTest, "BuzzerTest", 2048, NULL, 1, &TaskBuzzerTestHandle, 0
  // );
}

// --- MAIN LOOP ---
void loop() {
  // Empty - we use RTOS Tasks above
  vTaskDelete(NULL);
}
