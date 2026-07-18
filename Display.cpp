#include "Display.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include "Web.h"   // webGetIp()
#include <SevSeg.h>
#include <string.h>   // strlen

static SevSeg sevseg;

// Segment bits: a,b,c,d,e,f,g,dp (bit0..bit7)
static const byte digitSeg[10] = {
    0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
    0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
};
static const byte CHAR_BLANK = 0x00;
static const byte CHAR_C = 0b00111001;
static const byte CHAR_E = 0b01111001;
static const byte CHAR_P = 0b01110011;
static const byte CHAR_S = 0b01101101;   // identical glyph to '5' on 7-seg
static const byte CHAR_t = 0b01111000;
static const byte CHAR_r = 0b01010000;
static const byte CHAR_H = 0b01110110;
static const byte CHAR_I = 0b00000110;   // identical glyph to '1' on 7-seg
static const byte CHAR_G = 0b00111101;
static const byte CHAR_n = 0b01010100;
static const byte CHAR_DASH = 0b01000000;
static const byte SEG_DP = 0b10000000;

// Maps a character to its glyph for the scrolling views (IP address, error
// words). Digits, '.', and the specific letters those two use; anything else
// blanks rather than guessing at a glyph.
static byte charGlyph(char c) {
    if (c >= '0' && c <= '9') return digitSeg[c - '0'];
    switch (c) {
        case '.': return CHAR_DASH;
        case 'C': return CHAR_C;
        case 'E': return CHAR_E;
        case 'G': return CHAR_G;
        case 'H': return CHAR_H;
        case 'I': return CHAR_I;
        case 'n': return CHAR_n;
        case 'O': return digitSeg[0];
        case 'P': return CHAR_P;
        case 'r': return CHAR_r;
        case 'S': return CHAR_S;
        case 't': return CHAR_t;
        default:  return CHAR_BLANK;
    }
}

// Short 7-seg-legible word for each error reason - "PRESS"/"SEnSOr" scroll
// across the 4 digits (see renderErrorScroll); "HIGH" fits in one frame.
static const char* errorWord(ErrorReason r) {
    switch (r) {
        case ERR_OVER_TEMP:     return "HIGH";
        case ERR_OVER_PRESSURE: return "PRESS";
        case ERR_TEMP_SENSOR:   return "SEnSOr";
        default:                return "ERR";
    }
}

void setupDisplay() {
    byte digitPins[]   = { PIN_DISP_DIGIT1, PIN_DISP_DIGIT2, PIN_DISP_DIGIT3, PIN_DISP_DIGIT4 };
    byte segmentPins[] = { PIN_DISP_SEG_A, PIN_DISP_SEG_B, PIN_DISP_SEG_C, PIN_DISP_SEG_D,
                           PIN_DISP_SEG_E, PIN_DISP_SEG_F, PIN_DISP_SEG_G, PIN_DISP_SEG_DP };
    sevseg.begin(COMMON_ANODE, 4, digitPins, segmentPins,
                 /*resistorsOnSegments*/ false, /*updateWithDelays*/ false,
                 /*leadingZeros*/ false, /*disableDecPoint*/ false);
    sevseg.setBrightness(DISPLAY_BRIGHTNESS);
}

// [lead][XX.X] below 100, [lead][XXX] at/above 100.
static void renderTemp(float temp, byte lead) {
    byte s[4];
    s[0] = lead;
    if (temp >= 100.0f) {
        int t = (int)temp;
        s[1] = digitSeg[(t / 100) % 10];
        s[2] = digitSeg[(t / 10) % 10];
        s[3] = digitSeg[t % 10];
    } else {
        int t = (int)(temp * 10.0f);
        if (t < 0) t = 0;
        s[1] = digitSeg[(t / 100) % 10];
        s[2] = digitSeg[(t / 10) % 10] | 0b10000000;  // decimal point
        s[3] = digitSeg[t % 10];
    }
    sevseg.setSegments(s);
}

// [lead][ _ ][ _ ][seconds], right-aligned, blanked leading zeros.
static void renderSeconds(uint32_t sec, byte lead) {
    if (sec > 999) sec = 999;
    byte s[4];
    s[0] = lead;
    s[1] = (sec >= 100) ? digitSeg[(sec / 100) % 10] : CHAR_BLANK;
    s[2] = (sec >= 10)  ? digitSeg[(sec / 10) % 10]  : CHAR_BLANK;
    s[3] = digitSeg[sec % 10];
    sevseg.setSegments(s);
}

// SET_COFFEE: "S" + target, with the digit group being edited blinking.
// editDecimals = true  -> the tenths digit blinks
// editDecimals = false -> the whole-degree digits blink
static void renderSetCoffee(float temp, bool editDecimals) {
    static uint32_t last = 0; static bool on = true;
    if (millis() - last >= DISPLAY_BLINK_CYCLE_MS) { on = !on; last = millis(); }

    byte s[4];
    s[0] = CHAR_S;
    if (temp >= 100.0f) {
        int t = (int)temp;
        s[1] = digitSeg[(t / 100) % 10];
        s[2] = digitSeg[(t / 10) % 10];
        s[3] = digitSeg[t % 10];
        if (!on && !editDecimals) { s[1] = s[2] = s[3] = CHAR_BLANK; }   // no tenths at >=100
    } else {
        int t = (int)(temp * 10.0f);
        if (t < 0) t = 0;
        s[1] = digitSeg[(t / 100) % 10];
        s[2] = digitSeg[(t / 10) % 10] | SEG_DP;   // decimal point on ones digit
        s[3] = digitSeg[t % 10];
        if (!on) {
            if (editDecimals) {
                s[3] = CHAR_BLANK;          // blink the tenths
            } else {
                s[1] = CHAR_BLANK;          // blink the whole degrees (keep the dot)
                s[2] = SEG_DP;
            }
        }
    }
    sevseg.setSegments(s);
}

static void renderPreset(uint8_t index) {
    byte s[4] = { CHAR_P, CHAR_BLANK, CHAR_BLANK, digitSeg[(index + 1) % 10] };
    sevseg.setSegments(s);
}

// No blink phase - straight to the scrolling word on ERROR entry. Words
// <= 4 chars (e.g. "HIGH") just fill the display once and re-render
// identically every wrap, which reads as static.
static void renderErrorScroll(ErrorReason reason) {
    static uint32_t last = 0;
    static int pos = 0;
    if (millis() - last < DISPLAY_IP_SCROLL_MS) return;
    last = millis();

    const char* word = errorWord(reason);
    int len = (int)strlen(word);

    byte s[4] = { CHAR_BLANK, CHAR_BLANK, CHAR_BLANK, CHAR_BLANK };
    for (int i = 0; i < 4; i++) {
        int cp = pos + i;
        if (cp < len) s[i] = charGlyph(word[cp]);
    }
    sevseg.setSegments(s);
    if (++pos >= len) pos = 0;
}

// Static (not blinking - eco is an expected, hands-off background state).
static void renderEco() {
    byte s[4] = { CHAR_E, CHAR_C, digitSeg[0], CHAR_BLANK };
    sevseg.setSegments(s);
}

// Blinking "HOT" - coffee switch active but block too hot to start a brew.
// Reuses renderSetCoffee's on/off blink-timer pattern.
static void renderHot() {
    static uint32_t last = 0; static bool on = true;
    if (millis() - last >= DISPLAY_BLINK_CYCLE_MS) { on = !on; last = millis(); }

    byte s[4];
    if (on) { s[0] = CHAR_H; s[1] = digitSeg[0]; s[2] = CHAR_t; s[3] = CHAR_BLANK; }
    else    { s[0] = s[1] = s[2] = s[3] = CHAR_BLANK; }
    sevseg.setSegments(s);
}

static void renderIpScroll() {
    static uint32_t last = 0;
    static int pos = 0;
    static String ip = "";
    if (millis() - last < DISPLAY_IP_SCROLL_MS) return;
    last = millis();
    if (pos == 0) ip = webGetIp();

    byte s[4] = { CHAR_BLANK, CHAR_BLANK, CHAR_BLANK, CHAR_BLANK };
    for (int i = 0; i < 4; i++) {
        int cp = pos + i;
        if (cp < (int)ip.length()) s[i] = charGlyph(ip.charAt(cp));
    }
    sevseg.setSegments(s);
    if (++pos >= (int)ip.length()) pos = 0;
}

void refreshDisplay() {
    sevseg.refreshDisplay();   // fast multiplex - every cycle

    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate < DISPLAY_UPDATE_MS) return;
    lastUpdate = millis();

    SystemState s = stateSnapshot();

    switch (s.machineState) {
        case STATE_COFFEE:
            renderSeconds(s.brewTimerElapsedMs / 1000, CHAR_BLANK);   // elapsed shot time
            return;
        case STATE_STEAM:
            renderTemp(s.currentTemperature, CHAR_DASH);              // hot block temp
            return;
        case STATE_HOT_WATER:
            renderTemp(s.currentTemperature, CHAR_H);                 // hot block temp, cooling
            return;
        case STATE_ECO:
            renderEco();
            return;
        case STATE_ERROR:
            renderErrorScroll(s.errorReason);
            return;
        default: break;   // IDLE -> menu views below
    }

    // IDLE: coffee switch pressed but block too hot to start a brew - override
    // whatever menu view was selected, same precedent as STEAM/ERROR/HOT_WATER.
    if (s.switchCoffee && s.currentTemperature > BREW_READY_TEMP) {
        renderHot();
        return;
    }

    // IDLE: show the selected view.
    Settings cfg = settingsSnapshot();
    const Preset& p = cfg.preset[cfg.activePresetIndex];
    switch (s.displayView) {
        case VIEW_TEMP:       renderTemp(s.currentTemperature, CHAR_C);          break;
        case VIEW_SET_COFFEE: renderSetCoffee(p.coffeeTargetTemp, s.setEditDecimals); break;
        case VIEW_TIMER:      renderSeconds(p.shotMs / 1000, CHAR_t);            break;
        case VIEW_PRESET:     renderPreset(cfg.activePresetIndex);          break;
        case VIEW_IP:         renderIpScroll();                            break;
    }
}
