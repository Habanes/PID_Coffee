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
static const byte CHAR_b = 0b01111100;   // lowercase b
static const byte CHAR_h = 0b01110100;   // lowercase h
static const byte CHAR_L = 0b00111000;   // uppercase L - segments d,e,f (distinct from '1', unlike old lowercase-l glyph)
static const byte CHAR_DASH = 0b01000000;        // segment g only - also the left half of the "+" glyph pair below
static const byte CHAR_PLUS_RIGHT = 0b01110000;  // segments e,f,g - paired with CHAR_DASH to draw a "+" across 2 digits

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

// [lead][XXX], always whole degrees - no decimal, at any temperature. Needed
// to read correctly during STEAM (>= 100C) as much as during IDLE/COFFEE.
static void renderTemp(float temp, byte lead) {
    int t = (int)temp;
    if (t < 0) t = 0;
    byte s[4];
    s[0] = lead;
    s[1] = digitSeg[(t / 100) % 10];
    s[2] = digitSeg[(t / 10) % 10];
    s[3] = digitSeg[t % 10];
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

// Shared blink phase for every "this value is editable" render below - one
// timer so all of them blink in sync rather than each drifting on its own.
static bool blinkOn() {
    static uint32_t last = 0;
    static bool on = true;
    if (millis() - last >= DISPLAY_BLINK_CYCLE_MS) { on = !on; last = millis(); }
    return on;
}

// [c0][c1][seconds, 2 digits]. `blink` distinguishes the two callers: idle-
// menu phase-timing views (editable - value blinks, same convention as
// renderSetCoffee) from the brewing countdown (read-only - static). Real
// ranges can exceed 99s (see Config.h) but every practical phase value is
// well under it, so this saturates rather than widening.
static void renderLabeledSeconds(byte c0, byte c1, uint32_t sec, bool blink) {
    if (sec > 99) sec = 99;
    byte s[4];
    s[0] = c0;
    s[1] = c1;
    if (!blink || blinkOn()) {
        s[2] = digitSeg[(sec / 10) % 10];
        s[3] = digitSeg[sec % 10];
    } else {
        s[2] = s[3] = CHAR_BLANK;
    }
    sevseg.setSegments(s);
}

// Seconds remaining in a phase given its configured duration and how long
// it's been running - floors at 0 rather than wrapping negative.
static uint32_t remainingSec(uint32_t durationMs, uint32_t elapsedMs) {
    uint32_t durSec = durationMs / 1000;
    uint32_t elSec  = elapsedMs  / 1000;
    return (elSec < durSec) ? (durSec - elSec) : 0;
}

// SET_COFFEE: "S" + target, whole degrees only, value blinks as a group.
static void renderSetCoffee(float temp) {
    int t = (int)temp;
    if (t < 0) t = 0;
    byte s[4];
    s[0] = CHAR_S;
    if (blinkOn()) {
        s[1] = digitSeg[(t / 100) % 10];
        s[2] = digitSeg[(t / 10) % 10];
        s[3] = digitSeg[t % 10];
    } else {
        s[1] = s[2] = s[3] = CHAR_BLANK;
    }
    sevseg.setSegments(s);
}

// "P" + blank + 2-digit rank+1 (up to MAX_PRESETS=20, so a single digit isn't
// enough any more). `rank` is the 0-based position among active presets, not
// a raw slot index - see Settings.h's PRESET MODEL note. Value blinks - same
// "this is editable via the rotary" convention as renderSetCoffee.
static void renderPreset(uint8_t rank) {
    uint8_t num = rank + 1;
    byte s[4];
    s[0] = CHAR_P;
    s[1] = CHAR_BLANK;
    if (blinkOn()) {
        s[2] = digitSeg[(num / 10) % 10];
        s[3] = digitSeg[num % 10];
    } else {
        s[2] = s[3] = CHAR_BLANK;
    }
    sevseg.setSegments(s);
}

// Blinking "Or" + target: either a 2-digit preset number (rank+1, "Or00"-style)
// or, for the virtual "new" slot one past the last active rank, a hand-built
// "+" drawn across the last 2 digits - CHAR_DASH (segment g only) on digit 2
// forms the left half of the crossbar, CHAR_PLUS_RIGHT (segments e,f,g) on
// digit 3 forms the vertical stroke plus the right half of the crossbar, so
// together they read as a single "+" spanning both cells. Whole value blinks
// as a group, same precedent as renderSetCoffee.
static void renderPresetSave(uint8_t targetRank, uint8_t rankCount) {
    byte s[4];
    if (blinkOn()) {
        s[0] = digitSeg[0];   // 'O' - same glyph as digit 0, precedent: renderEco()
        s[1] = CHAR_r;
        if (targetRank >= rankCount) {
            s[2] = CHAR_DASH; s[3] = CHAR_PLUS_RIGHT;
        } else {
            uint8_t num = targetRank + 1;
            s[2] = digitSeg[(num / 10) % 10]; s[3] = digitSeg[num % 10];
        }
    } else {
        s[0] = s[1] = s[2] = s[3] = CHAR_BLANK;
    }
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

// Static "SLEP" - same non-blinking precedent as ECO, deeper standby tier.
static void renderSleep() {
    byte s[4] = { CHAR_S, CHAR_L, CHAR_E, CHAR_P };
    sevseg.setSegments(s);
}

// Blinking "HOT" - coffee switch active but block too hot to start a brew.
static void renderHot() {
    byte s[4];
    if (blinkOn()) { s[0] = CHAR_H; s[1] = digitSeg[0]; s[2] = CHAR_t; s[3] = CHAR_BLANK; }
    else           { s[0] = s[1] = s[2] = s[3] = CHAR_BLANK; }
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
        case STATE_COFFEE: {
            // Countdown per phase (preinfuse/bloom/preheat/boost); once boost
            // ends, switches to counting UP continuously (coffeePhaseElapsedMs
            // holds "since boost started" for BREW_PID/DONE - see
            // BrewStateMachine.cpp) - plain seconds, no label, since it's no
            // longer a fixed-duration phase.
            Preset p = activePreset();
            uint32_t el = s.coffeePhaseElapsedMs;
            switch (s.coffeeSubstate) {
                case SUB_PREINFUSE: renderLabeledSeconds(CHAR_P, CHAR_r, remainingSec(p.preinfuseMaxMs, el), false); break;
                case SUB_BLOOM:     renderLabeledSeconds(CHAR_b, CHAR_L, remainingSec(p.bloomMs,        el), false); break;
                case SUB_PREHEAT:   renderLabeledSeconds(CHAR_P, CHAR_h, remainingSec(p.preheatMs,      el), false); break;
                case SUB_BREW_MAX:  renderLabeledSeconds(CHAR_b, CHAR_S, remainingSec(p.brewMaxMs,      el), false); break;
                default:            renderSeconds(el / 1000, CHAR_BLANK); break;   // BREW_PID/DONE: count up
            }
            return;
        }
        case STATE_STEAM:
            renderTemp(s.currentTemperature, CHAR_DASH);              // hot block temp
            return;
        case STATE_HOT_WATER:
            renderTemp(s.currentTemperature, CHAR_H);                 // hot block temp, cooling
            return;
        case STATE_ECO:
            renderEco();
            return;
        case STATE_SLEEP:
            renderSleep();
            return;
        case STATE_ERROR:
            renderErrorScroll(s.errorReason);
            return;
        default: break;   // IDLE -> menu views below
    }

    // IDLE: the preset override/save screen (long-press from VIEW_PRESET,
    // see Input.cpp) takes priority - the user is mid-gesture, don't yank
    // the screen away from under them even if the too-hot condition fires.
    if (s.presetSaveMode) {
        renderPresetSave(s.presetSaveTargetRank, presetRankCount());
        return;
    }

    // IDLE: coffee switch pressed but block too hot to start a brew - override
    // whatever menu view was selected, same precedent as STEAM/ERROR/HOT_WATER.
    if (s.switchCoffee && s.currentTemperature > BREW_READY_TEMP) {
        renderHot();
        return;
    }

    // IDLE: show the selected view. Reads the WORKING copy (activePreset()),
    // not the stored slot directly, so live unsaved edits show immediately -
    // see Settings.h's LIVE-EDIT MODEL note.
    Preset p = activePreset();
    switch (s.displayView) {
        case VIEW_TEMP:       renderTemp(s.currentTemperature, CHAR_C);                       break;
        case VIEW_SET_COFFEE: renderSetCoffee(p.coffeeTargetTemp);                            break;
        case VIEW_PREINFUSE:  renderLabeledSeconds(CHAR_P, CHAR_r, p.preinfuseMaxMs / 1000, true);   break;
        case VIEW_BLOOM:      renderLabeledSeconds(CHAR_b, CHAR_L, p.bloomMs / 1000,        true);   break;
        case VIEW_PREHEAT:    renderLabeledSeconds(CHAR_P, CHAR_h, p.preheatMs / 1000,      true);   break;
        case VIEW_BOOST:      renderLabeledSeconds(CHAR_b, CHAR_S, p.brewMaxMs / 1000,      true);   break;
        case VIEW_PRESET: {
            int8_t rank = presetActiveRank();
            renderPreset((rank >= 0) ? (uint8_t)rank : 0);
            break;
        }
        case VIEW_IP:         renderIpScroll();                                                break;
    }
}
