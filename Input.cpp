#include "Input.h"
#include "State.h"
#include "Controls.h"
#include <RotaryEncoder.h>

// --- LIBRARY SETUP ---
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);

// --- INTERRUPT SERVICE ROUTINE (ISR) ---
// This function runs automatically whenever Pin A or B changes voltage.
void IRAM_ATTR checkPosition() {
  encoder.tick(); // The library calculates the state machine here
}

void setupInput() {
  // 1. Setup Rotary Pins (The library handles pinMode internally, but interrupts need this)
  attachInterrupt(digitalPinToInterrupt(PIN_IN1), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN2), checkPosition, CHANGE);

  // 2. Setup Button Pin
  pinMode(PIN_BTN, INPUT_PULLUP); // Button connects to Ground when pressed

  // 3. Setup Brew Button (active LOW, internal pull-up)
  pinMode(PIN_BREW, INPUT_PULLUP);
}

void syncInputState() {
  // --- 1. HANDLE ROTATION ---
  // The interrupts have already updated the internal position. 
  // We just ask "Has it changed since the last time we checked?"
  
  static long lastPos = 0;
  long currPos = encoder.getPosition();

  if (lastPos != currPos) {
    long delta = lastPos - currPos; // Positive: CCW (temp up), negative: CW (temp down)
    
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
      
      // Constrain temperature to reasonable range
      if (setTemp < SETTEMP_MIN) setTemp = SETTEMP_MIN;
      if (setTemp > SETTEMP_MAX) setTemp = SETTEMP_MAX;
      
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
    // Only accept change if BTN_DEBOUNCE_MS have passed (Debounce)
    if (millis() - lastBtnTime > BTN_DEBOUNCE_MS) {
      
      if (currBtnState == LOW) {
        // Button just pressed - record the time and reset long press flag
        btnPressTime = millis();
        longPressTriggered = false;
      } else {
        // Button just released - check how long it was pressed
        unsigned long pressDuration = millis() - btnPressTime;
        
        if (pressDuration < BTN_LONG_PRESS_MS && !longPressTriggered) {
          // SHORT PRESS: Cycle display mode CURRENT → SET → DEBUG(IP) → CURRENT
          STATE_LOCK();
          DisplayMode mode = state.displayMode;
          DisplayMode newMode;
          switch (mode) {
            case MODE_CURRENT: newMode = MODE_SET;     break;
            case MODE_SET:     newMode = MODE_DEBUG;   break;
            default:           newMode = MODE_CURRENT; break;
          }
          state.displayMode = newMode;
          STATE_UNLOCK();

          if (newMode == MODE_DEBUG) {
            savePIDToStorage(); // Persist setpoint adjusted via encoder
            Serial.println("→ Display Mode: IP (short press)");
          } else if (newMode == MODE_SET) {
            Serial.println("→ Display Mode: SET (short press)");
          } else {
            Serial.println("→ Display Mode: CURRENT (short press)");
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
    if (pressDuration >= BTN_LONG_PRESS_MS) {
      // LONG PRESS: Toggle sensitivity in SET mode between SENSITIVITY_FINE and SENSITIVITY_COARSE
      STATE_LOCK();
      DisplayMode currentMode = state.displayMode;
      float newSensitivity = state.tempSensitivity;
      if (currentMode == MODE_SET) {
        // Use threshold instead of float equality
        newSensitivity = (state.tempSensitivity < SENSITIVITY_THRESHOLD) ? SENSITIVITY_COARSE : SENSITIVITY_FINE;
        state.tempSensitivity = newSensitivity;
      }
      STATE_UNLOCK();
      if (currentMode == MODE_SET) {
        Serial.printf("→ Sensitivity: %.1f°C (long press)\n", newSensitivity);
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
    if (millis() - lastBrewBtnTime > BTN_DEBOUNCE_MS) { // debounce
      if (currBrewBtn == LOW) {
        // Toggling on press (not release) for instant feedback
        STATE_LOCK();
        bool newBrewMode = !state.brewMode;
        STATE_UNLOCK();

        setBrewMode(newBrewMode);
      }
      lastBrewBtnTime = millis();
      lastBrewBtnState = currBrewBtn;
    }
  }
}