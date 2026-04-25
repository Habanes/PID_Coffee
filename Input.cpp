#include "Input.h"
#include "State.h"
#include "Controls.h"
#include <RotaryEncoder.h> // Library by Matthias Hertel

// --- LIBRARY SETUP ---
// "LatchMode::TWO03" is the most common for standard EC11 encoders.
// If your encoder counts 2 steps per click, try LatchMode::FOUR3
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);

// --- INTERRUPT SERVICE ROUTINE (ISR) ---
// This function runs automatically whenever Pin A or B changes voltage.
// IRAM_ATTR puts it in High-Speed RAM (Instruction RAM).
void IRAM_ATTR checkPosition() {
  encoder.tick(); // The library calculates the state machine here
}

void setupInput() {
  Serial.println("Setting up Input...");

  // 1. Setup Rotary Pins (The library handles pinMode internally, but interrupts need this)
  attachInterrupt(digitalPinToInterrupt(PIN_IN1), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN2), checkPosition, CHANGE);

  // 2. Setup Button Pin
  pinMode(PIN_BTN, INPUT_PULLUP); // Button connects to Ground when pressed

  // 3. Setup Brew Button (GPIO 0 - boot button, used as brew toggle after boot)
  pinMode(PIN_BREW, INPUT_PULLUP);

  Serial.println("Input setup complete.");
}

void syncInputState() {
  // --- 1. HANDLE ROTATION ---
  // The interrupts have already updated the internal position. 
  // We just ask "Has it changed since the last time we checked?"
  
  static int lastPos = 0;
  int currPos = encoder.getPosition();

  if (lastPos != currPos) {
    int delta = lastPos - currPos; // Swapped to fix CW/CCW direction
    
    // Read current mode and sensitivity with mutex
    STATE_LOCK();
    DisplayMode mode = state.displayMode;
    float sensitivity = state.tempSensitivity;
    float setTemp = state.setTemp;
    STATE_UNLOCK();
    
    // Only adjust temperature when in SET mode
    if (mode == MODE_SET) {
      // Adjust set temperature using current sensitivity
      setTemp += (delta * sensitivity);
      
      // Constrain temperature to reasonable range (0-120°C)
      if (setTemp < 0.0) setTemp = 0.0;
      if (setTemp > 120.0) setTemp = 120.0;
      
      // Write back with mutex
      STATE_LOCK();
      state.setTemp = setTemp;
      STATE_UNLOCK();
    }

    // Update the previous position so we don't print again until it moves
    lastPos = currPos; 
  }

  // --- 2. HANDLE BUTTON ---
  // Track press time for short vs long press detection
  static bool lastBtnState = HIGH;
  static unsigned long btnPressTime = 0;
  static unsigned long lastBtnTime = 0;
  static bool longPressTriggered = false; // Track if long press action was already triggered
  
  bool currBtnState = digitalRead(PIN_BTN);

  if (currBtnState != lastBtnState) {
    // Only accept change if 50ms have passed (Debounce)
    if (millis() - lastBtnTime > 50) {
      
      if (currBtnState == LOW) {
        // Button just pressed - record the time and reset long press flag
        btnPressTime = millis();
        longPressTriggered = false;
        Serial.println("Button: PRESSED");
      } else {
        // Button just released - check how long it was pressed
        unsigned long pressDuration = millis() - btnPressTime;
        
        Serial.print("Button: RELEASED (");
        Serial.print(pressDuration);
        Serial.println("ms)");
        
        if (pressDuration < 500 && !longPressTriggered) {
          // SHORT PRESS: Toggle temperature sensitivity (only in SET mode)
          STATE_LOCK();
          DisplayMode mode = state.displayMode;
          float sensitivity = state.tempSensitivity;
          
          if (mode == MODE_SET) {
            if (sensitivity == 1.0) {
              state.tempSensitivity = 0.1;
              STATE_UNLOCK();
              Serial.println("→ Sensitivity: 0.1°C");
            } else {
              state.tempSensitivity = 1.0;
              STATE_UNLOCK();
              Serial.println("→ Sensitivity: 1.0°C");
            }
          } else {
            STATE_UNLOCK();
            Serial.println("→ Sensitivity toggle only available in SET mode");
          }
        }
        // Long press action was already triggered while button was held
      }
      
      lastBtnTime = millis();
      lastBtnState = currBtnState;
    }
  } else if (currBtnState == LOW && !longPressTriggered) {
    // Button is still pressed - check for long press threshold
    unsigned long pressDuration = millis() - btnPressTime;
    if (pressDuration >= 500) {
      // LONG PRESS: Cycle display mode (trigger immediately)
      STATE_LOCK();
      DisplayMode currentMode = state.displayMode;
      switch(currentMode) {
        case MODE_CURRENT:
          state.displayMode = MODE_SET;
          STATE_UNLOCK();
          Serial.println("→ Display Mode: SET (long press)");
          break;
        case MODE_SET:
          state.displayMode = MODE_DEBUG;
          STATE_UNLOCK();
          Serial.println("→ Display Mode: DEBUG (long press)");
          break;
        case MODE_DEBUG:
          state.displayMode = MODE_CURRENT;
          STATE_UNLOCK();
          Serial.println("→ Display Mode: CURRENT (long press)");
          break;
      }
      longPressTriggered = true; // Prevent multiple triggers
    }
  }

  // --- 3. HANDLE BREW BUTTON (GPIO 0) ---
  // Toggle brew mode on each press. Active LOW (internal pull-up).
  static bool lastBrewBtnState = HIGH;
  static unsigned long lastBrewBtnTime = 0;
  bool currBrewBtn = digitalRead(PIN_BREW);

  if (currBrewBtn != lastBrewBtnState) {
    if (millis() - lastBrewBtnTime > 50) { // 50ms debounce
      if (currBrewBtn == LOW) {
        // Toggling on press (not release) for instant feedback
        STATE_LOCK();
        bool newBrewMode = !state.brewMode;
        STATE_UNLOCK();

        setBrewMode(newBrewMode);

        if (newBrewMode) {
          Serial.println("[INPUT] Brew mode: ON");
        } else {
          Serial.println("[INPUT] Brew mode: OFF");
        }
      }
      lastBrewBtnTime = millis();
      lastBrewBtnState = currBrewBtn;
    }
  }
}