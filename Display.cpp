#include "Display.h"
#include "State.h"
#include "WebServer.h"

// Create the library object
SevSeg sevseg;

// Custom character definitions for mode indicators
const byte CHAR_C = 0b00111001; // 'C' for Current mode
const byte CHAR_S = 0b01101101; // 'S' for Set mode
const byte CHAR_D = 0b01011110; // 'd' for Debug mode
const byte CHAR_A = 0b01110111; // 'A' for debug display
const byte CHAR_B = 0b01111100; // 'b' for debug display
const byte CHAR_LOWER_C = 0b01011000; // 'c' for debug display

void setupDisplay() {
    byte numDigits = 4;
    
    // 1. Define your Pins (Map your Schematic pins to ESP32 GPIOs here)
    // {Digit1, Digit2, Digit3, Digit4} -> Schematic Pins 12, 9, 8, 6
    byte digitPins[] = {3, 11, 12, 8}; // EN1, EN2, EN3, EN4
    
    // {A, B, C, D, E, F, G, DP} -> Schematic Pins 11, 7, 4, 2, 1, 10, 5, 3
    byte segmentPins[] = {9, 13, 17, 15, 7, 10, 18, 16}; // A, B, C, D, E, F, G, DP

    // 2. Configure Library
    bool resistorsOnSegments = false; // Usually resistors are on segments
    byte hardwareConfig = COMMON_ANODE; // IMPORTANT: Matches your schematic
    bool updateWithDelays = false; // Default 'false' is recommended
    bool leadingZeros = false; // Use 'true' if you'd like to keep the leading zeros
    bool disableDecPoint = false; // Use 'true' if your decimal point doesn't exist or isn't connected.
    
    sevseg.begin(hardwareConfig, numDigits, digitPins, segmentPins, resistorsOnSegments,
                 updateWithDelays, leadingZeros, disableDecPoint);
                 
    sevseg.setBrightness(90); // 0 to 100
}

// Helper function to convert temperature to segment patterns
void setTempWithMode(float temp, byte modeChar) {
    // Create array for all 4 digits
    byte segments[4];
    
    // First digit is the mode character
    segments[0] = modeChar;
    
    // Convert temperature to XX.X format (3 digits)
    int tempInt = (int)(temp * 10); // e.g., 93.5 -> 935
    
    // Extract digits: 935 -> 9, 3, 5
    int digit1 = (tempInt / 100) % 10;
    int digit2 = (tempInt / 10) % 10;
    int digit3 = tempInt % 10;
    
    // Segment patterns for digits 0-9
    const byte digitSegments[10] = {
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
    
    segments[1] = digitSegments[digit1];
    segments[2] = digitSegments[digit2] | 0b10000000; // Add decimal point
    segments[3] = digitSegments[digit3];
    
    sevseg.setSegments(segments);
}

void refreshDisplay() {
    // A. THE FAST PART: Must run constantly to keep lights on
    sevseg.refreshDisplay(); 

    // B. THE SLOW PART: Update the number from shared state
    // We only check the variable every 100ms to avoid useless CPU work
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 100) {
        
        // Read state once with mutex protection
        STATE_LOCK();
        DisplayMode mode = state.displayMode;
        float currentTemp = state.currentTemp;
        float setTemp = state.setTemp;
        float sensitivity = state.tempSensitivity;
        bool brewMode = state.brewMode;
        STATE_UNLOCK();

        // Brew mode override: show current temp with fast-blinking 'b' (5 Hz)
        // This overrides all other display modes so you can always tell at a glance.
        if (brewMode) {
            static bool brewBlink = false;
            brewBlink = !brewBlink; // Toggle every 100ms → 5 Hz blink
            setTempWithMode(currentTemp, brewBlink ? CHAR_B : 0x00);
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
                // Show set temperature with 'S' in first digit (S XX.X)
                // Add blinking at 2Hz with 30% OFF duty cycle to show which digit is being edited
                {
                    static unsigned long lastBlink = 0;
                    static bool blinkState = false;
                    
                    // Toggle blink state at 2Hz (500ms cycle) with 30% off time (150ms off, 350ms on)
                    unsigned long blinkCycle = 500; // 2Hz = 500ms total cycle
                    unsigned long offTime = blinkCycle * 0.3; // 30% off time = 150ms
                    unsigned long onTime = blinkCycle - offTime; // 70% on time = 350ms
                    
                    if (millis() - lastBlink > (blinkState ? offTime : onTime)) {
                        blinkState = !blinkState;
                        lastBlink = millis();
                    }
                    
                    // Create array for all 4 digits
                    byte segments[4];
                    segments[0] = CHAR_S;  // Mode indicator
                    
                    // Convert temperature to XX.X format (using local copy)
                    int tempInt = (int)(setTemp * 10);
                    int digit1 = (tempInt / 100) % 10;  // Tens digit
                    int digit2 = (tempInt / 10) % 10;   // Ones digit
                    int digit3 = tempInt % 10;          // Decimal digit
                    
                    const byte digitSegments[10] = {
                        0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
                        0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
                    };
                    
                    // Normal display
                    segments[1] = digitSegments[digit1];
                    segments[2] = digitSegments[digit2] | 0b10000000; // Add decimal point
                    segments[3] = digitSegments[digit3];
                    
                    // Blink the digit being edited (based on tempSensitivity)
                    if (blinkState) {
                        if (sensitivity == 1.0) {
                            // Editing whole degrees - blink tens and ones digits
                            segments[1] = 0;  // Blank tens digit
                            segments[2] = 0b10000000;  // Keep only decimal point
                        } else {
                            // Editing decimal - blink decimal digit
                            segments[3] = 0;  // Blank decimal digit
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
                    
                    // Update IP every 500ms (2Hz)
                    if (millis() - lastIPUpdate > 500) {
                        // Get fresh IP on first cycle
                        if (ipCharIndex == 0) {
                            ipAddress = getIPAddress();
                        }
                        
                        // Convert IP to character array for display
                        // e.g., "192.168.1.100" - we'll show 4 chars at a time
                        byte segments[4] = {0, 0, 0, 0};
                        
                        // Digit lookup table
                        const byte digitSegments[10] = {
                            0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
                            0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
                        };
                        const byte CHAR_DASH = 0b01000000; // '-' for dot separator
                        
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