#include "Input.h"
#include "State.h"
#include "Controls.h"
#include "Buzzer.h"
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
  Serial.printf("[INPUT] Attaching encoder ISR: PIN_IN1=GPIO%d, PIN_IN2=GPIO%d\n", PIN_IN1, PIN_IN2);
  attachInterrupt(digitalPinToInterrupt(PIN_IN1), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN2), checkPosition, CHANGE);
  Serial.println("[INPUT] Encoder interrupts attached");

  // 2. Setup Button Pin
  Serial.printf("[INPUT] Setting up button: PIN_BTN=GPIO%d\n", PIN_BTN);
  pinMode(PIN_BTN, INPUT_PULLUP); // Button connects to Ground when pressed
  Serial.printf("[INPUT] BTN initial state: %s\n", digitalRead(PIN_BTN) == HIGH ? "HIGH (not pressed)" : "LOW (pressed/short?)");

  // 3. Setup switch ADC pin (voltage divider ladder: SW_STEAM + SW_COFFEE on one pin)
  Serial.printf("[INPUT] Setting up switch ADC: PIN_SWITCHES=GPIO%d\n", PIN_SWITCHES);
  analogSetPinAttenuation(PIN_SWITCHES, ADC_11db); // Full 0-3.3V range
  Serial.printf("[INPUT] Switch ADC initial read: %d\n", analogRead(PIN_SWITCHES));

  Serial.println("[INPUT] Setup complete");
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

      playEncoderTick(); // Audible feedback per step
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
        playButtonClick(); // Immediate press feedback
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
      playLongPress(); // Audible confirmation of sensitivity toggle
    }
  }

  // --- 3. HANDLE SWITCH INPUTS (voltage divider ADC on PIN_SWITCHES) ---
  // Ladder: R_steam=10kΩ, R_coffee=5.1kΩ, R_pd=5.1kΩ to GND; source=5V opto outputs
  // ADC bands: BOTH(0-631), COFFEE(632-1867), STEAM(1868-3103), NEITHER(3104-4095)
  int adcVal = analogRead(PIN_SWITCHES);
  bool swSteam, swCoffee;

  if (adcVal <= SWITCH_ADC_BOTH_MAX) {
    swSteam = true;  swCoffee = true;   // Both optos conducting
  } else if (adcVal <= SWITCH_ADC_COFFEE_MAX) {
    swSteam = false; swCoffee = true;   // Coffee opto only
  } else if (adcVal <= SWITCH_ADC_STEAM_MAX) {
    swSteam = true;  swCoffee = false;  // Steam opto only
  } else {
    swSteam = false; swCoffee = false;  // Neither
  }

  float switchVoltage = adcVal * (3.3f / 4095.0f);

  STATE_LOCK();
  state.swSteam       = swSteam;
  state.swCoffee      = swCoffee;
  state.switchVoltage = switchVoltage;
  STATE_UNLOCK();
}