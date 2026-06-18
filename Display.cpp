#include "Display.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include "Web.h"   // webGetIp()
#include <SevSeg.h>

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
static const byte CHAR_t = 0b01111000;
static const byte CHAR_r = 0b01010000;
static const byte CHAR_DASH = 0b01000000;

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

static void renderPreset(uint8_t index) {
    byte s[4] = { CHAR_P, CHAR_BLANK, CHAR_BLANK, digitSeg[(index + 1) % 10] };
    sevseg.setSegments(s);
}

static void renderErrorBlink() {
    static uint32_t last = 0; static bool on = true;
    if (millis() - last >= DISPLAY_BLINK_CYCLE_MS) { on = !on; last = millis(); }
    byte s[4];
    if (on) { s[0] = CHAR_E; s[1] = CHAR_r; s[2] = CHAR_r; s[3] = CHAR_BLANK; }
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
        if (cp < (int)ip.length()) {
            char c = ip.charAt(cp);
            if (c >= '0' && c <= '9') s[i] = digitSeg[c - '0'];
            else if (c == '.')        s[i] = CHAR_DASH;
        }
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
        case STATE_ERROR:
            renderErrorBlink();
            return;
        default: break;   // IDLE -> menu views below
    }

    // IDLE: show the selected view.
    Settings cfg = settingsSnapshot();
    const Preset& p = cfg.preset[cfg.activePresetIndex];
    switch (s.displayView) {
        case VIEW_TEMP:       renderTemp(s.currentTemperature, CHAR_BLANK); break;
        case VIEW_SET_COFFEE: renderTemp(p.coffeeTargetTemp, CHAR_C);       break;
        case VIEW_TIMER:      renderSeconds(p.shotMs / 1000, CHAR_t);       break;
        case VIEW_PRESET:     renderPreset(cfg.activePresetIndex);          break;
        case VIEW_IP:         renderIpScroll();                            break;
    }
}
