#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// CoffeePID - QuickMill Orione 3000  |  central configuration
// One place for every pin, constant, default and settable range.
// See ../Architecture.txt, ../Processes.txt, ../FreeRTOS Management.txt.
// =====================================================================


// =====================================================================
// 1. PIN MAP                                    (see ../Hardware IO.txt)
// =====================================================================

// Outputs
#define PIN_HEATER_SSR      14      // Solid-state relay (time-proportional)
#define PIN_PUMP            38      // Pump   - active LOW
#define PIN_VALVE           48      // Valve  - active LOW
#define PIN_BUZZER          47

// Inputs
#define PIN_TSIC            21      // TSIC temperature sensor (ZACWire)
#define PIN_PRESSURE         1      // Pressure transducer (ADC)
#define PIN_SWITCHES         2      // Steam + coffee combined ladder (ADC)
#define PIN_ENC_A            6      // Rotary encoder signal A
#define PIN_ENC_B            5      // Rotary encoder signal B
#define PIN_BTN              4      // Rotary push button (active LOW)

// 7-segment display (4 digit, common anode)
#define PIN_DISP_DIGIT1      3
#define PIN_DISP_DIGIT2     11
#define PIN_DISP_DIGIT3     12
#define PIN_DISP_DIGIT4      8
#define PIN_DISP_SEG_A       9
#define PIN_DISP_SEG_B      13
#define PIN_DISP_SEG_C      17
#define PIN_DISP_SEG_D      15
#define PIN_DISP_SEG_E       7
#define PIN_DISP_SEG_F      10
#define PIN_DISP_SEG_G      18
#define PIN_DISP_SEG_DP     16

// 7-segment display behaviour
#define DISPLAY_BRIGHTNESS      90      // 0-100
#define DISPLAY_UPDATE_MS       100     // content refresh interval (ms)
#define DISPLAY_BLINK_CYCLE_MS  500     // ERROR "Err" blink period (ms)
#define DISPLAY_IP_SCROLL_MS    500     // IP view scroll step (ms)


// =====================================================================
// 2. RTOS TASKS                          (see ../FreeRTOS Management.txt)
// =====================================================================

#define STARTUP_DELAY_MS            2000

// ControlTask - Core 1, real-time control loop @ 10 Hz
#define TASK_CONTROL_CORE           1
#define TASK_CONTROL_PRIORITY       3
#define TASK_CONTROL_STACK          4096
#define TASK_CONTROL_CYCLE_MS       100     // 10 Hz

// UiTask - Core 0, 7-seg refresh + input + buzzer
#define TASK_UI_CORE                0
#define TASK_UI_PRIORITY            2
#define TASK_UI_STACK               4096
#define TASK_UI_CYCLE_MS            2

// WebTask - Core 0, HTTP server
#define TASK_WEB_CORE               0
#define TASK_WEB_PRIORITY           1
#define TASK_WEB_STACK              8192
#define TASK_WEB_CYCLE_MS           10


// =====================================================================
// 3. SWITCH STATE MACHINE                          (process 1)
// Measured pin voltages: both-on ~0V, steam ~0.85V, coffee ~1.6V,
// both-off ~3.3V. Thresholds at the band midpoints (ADC 0..4095 @ 3.3V).
// Decode ascending: v<=L1 BOTH, v<=L2 STEAM, v<=L3 COFFEE, else NEITHER.
// (ADC is non-linear / ref varies - tune these on real hardware.)
// =====================================================================

#define SWITCH_LEVEL_1_ADC      534     // 0.43 V  border BOTH | STEAM
#define SWITCH_LEVEL_2_ADC      1526    // 1.23 V  border STEAM | COFFEE
#define SWITCH_LEVEL_3_ADC      3040    // 2.45 V  border COFFEE | NEITHER
#define SWITCH_DEBOUNCE_MS      600     // band must be stable this long


// =====================================================================
// 4. TEMPERATURE SENSOR                            (process 2)
// NOTE: TSIC 306 tops out around 150 C, so ALL steam-related limits are kept
//       below 150 (steamTargetTemp default 130, steamTempMax 145). The sensor
//       can read them, so the bang-bang cutoff AND the over-temp trip / ISR
//       cutoff all work within range.
// =====================================================================

#define EMA_ALPHA               0.6f    // EMA smoothing (1 = raw, 0 = max)
#define EMA_SEED                20.0f   // Startup seed (room temp)
#define TEMP_MIN_VALID          0.0f    // Below -> sensor glitch
#define TEMP_MAX_VALID          180.0f  // Above -> sensor glitch
#define TEMP_ERROR_INTERVAL_MS  1000    // Bad (absent/invalid) this long -> error


// =====================================================================
// 5. PRESSURE SENSOR                               (process 3)
// Wiring: sensor OUT -> R1=2.2k -> GPIO, R2=5.1k -> GND. Ratiometric 5V:
// 0.5 V = 0 Bar, 4.5 V = full scale. No fault flag (over-range trips
// safePressureMax on its own).
// =====================================================================

#define PRESSURE_RANGE_BAR      16.0f   // Full-scale Bar (match ordered sensor)
#define PRESSURE_DIVIDER_RATIO  0.6986f // R2/(R1+R2) = 5100/7300
#define PRESSURE_SENSOR_V_LOW   0.5f    // Sensor output at 0 Bar
#define PRESSURE_SENSOR_V_HIGH  4.5f    // Sensor output at full scale
#define PRESSURE_ADC_SAMPLES    4       // Samples averaged per read


// =====================================================================
// 6. HEATER OUTPUT + PID                           (processes 5, 6)
// =====================================================================

#define SSR_WINDOW_MS           1000    // Time-proportional SSR window
#define HEATER_TIMER_INTERVAL_MS 10     // ISR period (= 100 Hz)
#define PID_IMAX                55.0     // Integrator clamp
#define PID_EMA_FACTOR          0.6      // EMA on PID derivative input


// =====================================================================
// 7. LIMITS                                        (process 4)
// Mode-aware over-temp: coffeeTempMax in COFFEE, steamTempMax in IDLE/STEAM.
// (coffeeTempMax + steamTempMax are SETTINGS - defaults in section 11.)
// =====================================================================

#define BREW_READY_TEMP         98.0f   // Block must be <= this to START a shot
#define STEAM_HYSTERESIS        5.0f    // Steam bang-bang band below target
#define ERROR_CLEAR_HYSTERESIS  5.0f    // Temp must drop this far below limit
                                        // before ERROR can clear

// Over-limit trips must persist this long before tripping ERROR, so a brief
// sensor spike does not cause a false error (feature spec: temp/pressure
// error intervals).
#define OVERTEMP_TRIP_MS        300
#define OVERPRESSURE_TRIP_MS    100


// =====================================================================
// 8. INPUT / ENCODER / MENU                        (process 7)
// On-device menu is IDLE-only. Encoder edits the active preset's coffee
// target temp and shot time, and selects the preset.
// =====================================================================

#define BTN_DEBOUNCE_MS         50
#define BTN_LONG_PRESS_MS       500
// SET_COFFEE has two encoder granularities, toggled by a long button press:
//   WHOLE = single degrees, FINE = tenths. The selected digit blinks (see Display).
#define COFFEE_TEMP_STEP_WHOLE  1.0f    // Encoder step editing whole degrees (C)
#define COFFEE_TEMP_STEP_FINE   0.1f    // Encoder step editing tenths (C)
#define SHOT_TIME_STEP_MS       1000    // Encoder step in TIMER view (1 s)


// =====================================================================
// 9. BUZZER TONES                                  (process 11)
// Enter COFFEE/STEAM: ascending perfect fifth (A4->E5). Exit: descending.
// ERROR: repeating beep-boop siren. All playback non-blocking.
// =====================================================================

#define BUZZER_DEFAULT_MUTE     false

#define TONE_TICK_HZ            2000     // Encoder step click
#define TONE_CLICK_HZ          1500     // Button press click
#define TONE_FIFTH_LOW_HZ       440      // A4  (enter low / exit high target)
#define TONE_FIFTH_HIGH_HZ      659      // E5  (enter high / exit low target)
#define TONE_SIREN_HI_HZ        880      // Error "beep"
#define TONE_SIREN_LO_HZ        660      // Error "boop"
#define TONE_CLICK_MS           15
#define TONE_NOTE_MS            140      // Per note in the enter/exit jingles
#define TONE_SIREN_MS           250      // Per siren tone


// =====================================================================
// 10. WIFI / WEB                                   (process 9)
// NOTE: fill in real credentials locally - do NOT commit secrets.
// =====================================================================

#define WIFI_SSID               "BabaLan"
#define WIFI_PASSWORD           "bittegibmirinternet"
#define AP_SSID                 "QuickMill-PID"
#define AP_PASSWORD             "changeme123"

#define WEBSERVER_PORT          80
#define WIFI_CONNECT_ATTEMPTS   20
#define WIFI_CONNECT_DELAY_MS   500
#define WEB_BODY_READ_TIMEOUT_MS 1000
#define WEB_SEND_BUFFER_SIZE    1024


// =====================================================================
// 11. SETTINGS - DEFAULTS & RANGES
// Written only via the config layer (web GUI / 7-seg menu), IDLE-only,
// validated against the *_MIN / *_MAX bounds, persisted to NVS.
// =====================================================================

#define NUM_PRESETS             3
#define NVS_NAMESPACE           "coffee-pid"

// ---- Global: heating PID ----
#define DEFAULT_HEATING_KP      62.0
#define DEFAULT_HEATING_KI      1.19
#define DEFAULT_HEATING_KD      713.0
// ---- Global: brew PID (Ki disabled during brew) ----
#define DEFAULT_BREW_KP         50.0
#define DEFAULT_BREW_KI         0.0
#define DEFAULT_BREW_KD         1000.0
// shared PID gain bounds
#define PID_KP_MIN              0.0
#define PID_KP_MAX              500.0
#define PID_KI_MIN              0.0
#define PID_KI_MAX              50.0
#define PID_KD_MIN              0.0
#define PID_KD_MAX              2000.0

// ---- Global: sensor offset (subtracted at the temperature process) ----
#define DEFAULT_TEMP_OFFSET     5.0f
#define TEMP_OFFSET_MIN         0.0f
#define TEMP_OFFSET_MAX         10.0f

// ---- Global: safety limits ----
#define DEFAULT_COFFEE_TEMP_MAX 110.0f  // ERROR in COFFEE
#define COFFEE_TEMP_MAX_MIN     100.0f
#define COFFEE_TEMP_MAX_MAX     140.0f
#define DEFAULT_STEAM_TEMP_MAX  145.0f  // ERROR in IDLE/STEAM + ISR cutoff (< 150 sensor ceiling)
#define STEAM_TEMP_MAX_MIN      130.0f
#define STEAM_TEMP_MAX_MAX      149.0f
#define DEFAULT_SAFE_PRESSURE_MAX 14.0f
#define SAFE_PRESSURE_MAX_MIN   1.0f
#define SAFE_PRESSURE_MAX_MAX   16.0f

// ---- Per-preset: targets ----
#define DEFAULT_COFFEE_TARGET_TEMP 93.0f
#define COFFEE_TARGET_TEMP_MIN  80.0f
#define COFFEE_TARGET_TEMP_MAX  100.0f
#define DEFAULT_STEAM_TARGET_TEMP 130.0f
#define STEAM_TARGET_TEMP_MIN   115.0f
#define STEAM_TARGET_TEMP_MAX   135.0f

// ---- Per-preset: brew timings (ms) ----
#define DEFAULT_PREINFUSE_MAX_MS 3000
#define DEFAULT_BLOOM_MS        5000
#define DEFAULT_PREHEAT_MS      2000
#define DEFAULT_BREW_MAX_MS     8000
#define DEFAULT_SHOT_MS         30000   // TOTAL brew time (continuous from start)
#define BREW_TIME_MIN_MS        100
#define BREW_TIME_MAX_MS        120000
#define SHOT_TIME_MIN_MS        10000
#define SHOT_TIME_MAX_MS        120000

// ---- Per-preset: pre-infuse early-exit pressure ----
#define DEFAULT_PREINFUSE_TARGET_BAR 2.5f
#define PREINFUSE_TARGET_BAR_MIN 0.5f
#define PREINFUSE_TARGET_BAR_MAX 10.0f

// Coherence (enforced in Settings validation, see ../Architecture.txt):
//   coffeeTempMax >= coffeeTargetTemp + ~10
//   steamTempMax  >= steamTargetTemp  + ~10
//   shotTimeMs    >= brewMaxTimeMs

#endif // CONFIG_H
