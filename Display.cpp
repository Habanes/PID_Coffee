#include "Display.h"
#include "State.h"
#include "WebServer.h"
#include "Controls.h"
#include "Buzzer.h"

// Create the library object
SevSeg sevseg;

// Custom character definitions for mode indicators
const byte CHAR_C = 0b00111001; // 'C' for Current mode
const byte CHAR_S = 0b01101101; // 'S' for Set mode
const byte CHAR_B = 0b01111100; // 'b' for brew / COFFEE state
const byte CHAR_E = 0b01111001; // 'E' for ERROR state
const byte CHAR_DASH = 0b01000000; // '-' for STEAM (hardware has control)

// Segment patterns for digits 0-9 (shared across all display modes)
static const byte digitSegments[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
};

void setupDisplay() {
    byte numDigits = 4;
    
    // Pin assignments defined in Config.h
    byte digitPins[]   = {PIN_DISP_DIGIT1, PIN_DISP_DIGIT2, PIN_DISP_DIGIT3, PIN_DISP_DIGIT4};
    byte segmentPins[] = {PIN_DISP_A, PIN_DISP_B, PIN_DISP_C, PIN_DISP_D, PIN_DISP_E, PIN_DISP_F, PIN_DISP_G, PIN_DISP_DP};

    // Configure Library
    bool resistorsOnSegments = false; // Usually resistors are on segments
    byte hardwareConfig = COMMON_ANODE; // IMPORTANT: Matches your schematic
    bool updateWithDelays = false; // Default 'false' is recommended
    bool leadingZeros = false; // Use 'true' if you'd like to keep the leading zeros
    bool disableDecPoint = false; // Use 'true' if your decimal point doesn't exist or isn't connected.
    
    sevseg.begin(hardwareConfig, numDigits, digitPins, segmentPins, resistorsOnSegments,
                 updateWithDelays, leadingZeros, disableDecPoint);
                 
    sevseg.setBrightness(DISPLAY_BRIGHTNESS);
}

// Convert temperature to segment patterns: [modeChar][XX.X] below 100°C, [modeChar][XXX] above
static void setTempWithMode(float temp, byte modeChar) {
    byte segments[4];
    segments[0] = modeChar;

    if (temp >= 100.0f) {
        // 3-digit integer: drop decimal to fit [mode][d1][d2][d3]
        int t = (int)temp;
        segments[1] = digitSegments[(t / 100) % 10];
        segments[2] = digitSegments[(t / 10)  % 10];
        segments[3] = digitSegments[ t        % 10];
    } else {
        int tempInt = (int)(temp * 10);
        if (tempInt < 0) tempInt = 0; // clamp: negative index into digitSegments is UB
        segments[1] = digitSegments[(tempInt / 100) % 10];
        segments[2] = digitSegments[(tempInt / 10)  % 10] | 0b10000000; // decimal point
        segments[3] = digitSegments[ tempInt        % 10];
    }

    sevseg.setSegments(segments);
}

void refreshDisplay() {
    // A. THE FAST PART: Must run constantly to keep lights on
    sevseg.refreshDisplay(); 

    // B. THE SLOW PART: Update the number from shared state
    // We only check the variable every 100ms to avoid useless CPU work
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > DISPLAY_UPDATE_MS) {
        
        // Read state once with mutex protection
        STATE_LOCK();
        DisplayMode    mode        = state.displayMode;
        float          currentTemp = state.currentTemp;
        float          setTemp     = state.setTemp;
        float          sensitivity = state.tempSensitivity;
        MachineState   machState   = state.machineState;
        STATE_UNLOCK();

        // Siren on emergency stop OR any ERROR state (includes pressure over-limit)
        updateSiren(isEmergencyStopActive() || machState == STATE_ERROR);

        // STATE_COFFEE: blink 'b' at 1Hz using DISPLAY_BLINK_CYCLE_MS
        if (machState == STATE_COFFEE) {
            static unsigned long lastBrewBlink = 0;
            static bool brewBlink = true;
            if (millis() - lastBrewBlink >= DISPLAY_BLINK_CYCLE_MS) {
                brewBlink = !brewBlink;
                lastBrewBlink = millis();
            }
            setTempWithMode(currentTemp, brewBlink ? CHAR_B : 0x00);
            lastUpdate = millis();
            return;
        }

        // STATE_STEAM: hardware has control — show '----'
        if (machState == STATE_STEAM) {
            byte segs[4] = {CHAR_DASH, CHAR_DASH, CHAR_DASH, CHAR_DASH};
            sevseg.setSegments(segs);
            lastUpdate = millis();
            return;
        }

        // STATE_ERROR: blink 'E' at 1Hz using DISPLAY_BLINK_CYCLE_MS
        if (machState == STATE_ERROR) {
            static unsigned long lastErrBlink = 0;
            static bool errBlink = true;
            if (millis() - lastErrBlink >= DISPLAY_BLINK_CYCLE_MS) {
                errBlink = !errBlink;
                lastErrBlink = millis();
            }
            setTempWithMode(currentTemp, errBlink ? CHAR_E : 0x00);
            lastUpdate = millis();
            return;
        }

        // Display based on current mode
        switch(mode) {
            case MODE_CURRENT:
                // Show current temperature with 'C' in first digit (C XX.X)
                setTempWithMode(currentTemp, CHAR_C);
                break;
                
            case MODE_SET:
                // Show set temperature with 'S' in first digit (S XX.X or S XXX above 100°C)
                // Add blinking at 2Hz with 30% OFF duty cycle to show which digit is being edited
                {
                    static unsigned long lastBlink = 0;
                    static bool blinkState = false;

                    // Toggle blink state at 2Hz (500ms cycle) with 30% off time (150ms off, 350ms on)
                    const unsigned long offTime = DISPLAY_BLINK_CYCLE_MS * DISPLAY_BLINK_OFF_RATIO;
                    const unsigned long onTime  = DISPLAY_BLINK_CYCLE_MS - offTime;

                    if (millis() - lastBlink > (blinkState ? offTime : onTime)) {
                        blinkState = !blinkState;
                        lastBlink = millis();
                    }

                    byte segments[4];
                    segments[0] = CHAR_S;

                    if (setTemp >= 100.0f) {
                        // 3-digit integer: [S][d1][d2][d3], no decimal
                        int t = (int)setTemp;
                        segments[1] = digitSegments[(t / 100) % 10];
                        segments[2] = digitSegments[(t / 10)  % 10];
                        segments[3] = digitSegments[ t        % 10];
                        if (blinkState) {
                            if (sensitivity >= SENSITIVITY_THRESHOLD) {
                                segments[1] = 0;
                                segments[2] = 0;
                            } else {
                                segments[3] = 0;
                            }
                        }
                    } else {
                        // XX.X format
                        int tempInt = (int)(setTemp * 10);
                        int digit1 = (tempInt / 100) % 10;
                        int digit2 = (tempInt / 10)  % 10;
                        int digit3 =  tempInt        % 10;
                        segments[1] = digitSegments[digit1];
                        segments[2] = digitSegments[digit2] | 0b10000000;
                        segments[3] = digitSegments[digit3];
                        if (blinkState) {
                            if (sensitivity >= SENSITIVITY_THRESHOLD) {
                                segments[1] = 0;
                                segments[2] = 0b10000000;
                            } else {
                                segments[3] = 0;
                            }
                        }
                    }

                    sevseg.setSegments(segments);
                }
                break;
                
            case MODE_DEBUG:
                // Show IP address rolling through at 2Hz
                {
                    static unsigned long lastIPUpdate = 0;
                    static int ipCharIndex = 0;
                    static String ipAddress = "";
                    
                    // Update IP every DISPLAY_IP_SCROLL_MS
                    if (millis() - lastIPUpdate > DISPLAY_IP_SCROLL_MS) {
                        // Get fresh IP on first cycle
                        if (ipCharIndex == 0) {
                            ipAddress = getIPAddress();
                        }
                        
                        // Convert IP to character array for display
                        // e.g., "192.168.1.100" - we'll show 4 chars at a time
                        byte segments[4] = {0, 0, 0, 0};

                        // Show 4 characters at a time from IP string
                        for (int i = 0; i < 4; i++) {
                            int charPos = ipCharIndex + i;
                            if (charPos < ipAddress.length()) {
                                char c = ipAddress.charAt(charPos);
                                if (c >= '0' && c <= '9') {
                                    segments[i] = digitSegments[c - '0'];
                                } else if (c == '.') {
                                    segments[i] = CHAR_DASH;
                                } else {
                                    segments[i] = 0; // Blank for other chars
                                }
                            } else {
                                segments[i] = 0; // Blank if past end
                            }
                        }
                        
                        sevseg.setSegments(segments);
                        
                        // Advance to next position
                        ipCharIndex++;
                        
                        // Reset when we've scrolled through the whole IP
                        if (ipCharIndex >= ipAddress.length()) {
                            ipCharIndex = 0;
                        }
                        
                        lastIPUpdate = millis();
                    }
                }
                break;
        }
        
        lastUpdate = millis();
    }
}