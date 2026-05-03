#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// PIN MAPPING
// =====================================================================

// SSR relay
#define RELAY_PIN               14

// TSIC 306 temperature sensor (ZACWire protocol)
#define TSIC_SIGNAL_PIN         21

// Rotary encoder
#define PIN_IN1                 6   // Signal A
#define PIN_IN2                 5   // Signal B
#define PIN_BTN                 4   // Push button

// Switch input ADC — both optocouplers share one ADC pin via resistor ladder
// Ladder: R_steam=10kΩ → node, R_coffee=5.1kΩ → node, R_pulldown=5.1kΩ → GND; Vsrc=5V
#define PIN_SWITCHES            2   // ADC: SW_STEAM + SW_COFFEE voltage divider
#define PIN_PRESSURE            1   // ADC: pressure transducer (stubbed until sensor arrives)

// Pump and valve outputs (active HIGH — low-side transistor switches, 5V load)
#define PIN_PUMP                48
#define PIN_VALVE               38

// Buzzer
#define BUZZER_PIN              47
#define BUZZER_MUTE             true    // Set true to silence all buzzer output

// 7-Segment display — digit enable pins (common anode, active HIGH)
#define PIN_DISP_DIGIT1         3
#define PIN_DISP_DIGIT2         11
#define PIN_DISP_DIGIT3         12
#define PIN_DISP_DIGIT4         8

// 7-Segment display — segment pins (A, B, C, D, E, F, G, DP)
#define PIN_DISP_A              9
#define PIN_DISP_B              13
#define PIN_DISP_C              17
#define PIN_DISP_D              15
#define PIN_DISP_E              7
#define PIN_DISP_F              10
#define PIN_DISP_G              18
#define PIN_DISP_DP             16

// Display brightness (0–100)
#define DISPLAY_BRIGHTNESS      90

// =====================================================================
// DISPLAY
// =====================================================================

#define DISPLAY_UPDATE_MS           100     // Content refresh interval (ms)
#define DISPLAY_BLINK_CYCLE_MS      500     // SET mode blink period (ms, 2 Hz)
#define DISPLAY_BLINK_OFF_RATIO     0.3f    // Fraction of blink cycle that is off (30%)
#define DISPLAY_IP_SCROLL_MS        500     // IP address scroll speed in DEBUG mode (ms)

// =====================================================================
// INPUT
// =====================================================================

#define BTN_DEBOUNCE_MS         50      // Button debounce time (ms)
#define BTN_LONG_PRESS_MS       500     // Long press threshold (ms)
#define SETTEMP_MIN             0.0f    // Minimum allowed setpoint (°C)
#define SETTEMP_MAX             120.0f  // Maximum allowed setpoint (°C)
#define SENSITIVITY_FINE        0.1f    // Fine encoder sensitivity (°C/step)
#define SENSITIVITY_COARSE      1.0f    // Coarse encoder sensitivity (°C/step)
#define SENSITIVITY_THRESHOLD   0.5f    // Midpoint for sensitivity toggle

// Switch ADC decode thresholds (12-bit 0–4095, Vref=3.3V)
// Ladder voltages: BOTH=0.000V(0), COFFEE=1.016V(1261), STEAM=1.992V(2472), NEITHER=3.008V(3733)
#define SWITCH_ADC_BOTH_MAX     631     //    0 –  631 → both pressed   (ERROR territory)
#define SWITCH_ADC_COFFEE_MAX   1867    //  632 – 1867 → coffee only
#define SWITCH_ADC_STEAM_MAX    3103    // 1868 – 3103 → steam only
                                        // 3104 – 4095 → neither pressed

// =====================================================================
// SAFETY LIMITS
// =====================================================================

#define EMERGENCY_STOP_TEMP     100.0   // Heater cut-off temperature (°C)
#define TEMP_MIN_VALID          5.0     // Below this is treated as a sensor fault (°C)
#define TEMP_MAX_VALID          150.0   // Above this is treated as a sensor fault (°C)
#define MAX_CONSECUTIVE_FAILURES 3      // Consecutive bad readings before sensorError is set

// =====================================================================
// SENSOR
// =====================================================================

// EMA smoothing factor for raw TSIC readings (0 = no smoothing, 1 = maximum smoothing)
#define EMA_ALPHA               0.6f

// Seed value for EMA filter on startup (room temperature assumption)
#define SENSOR_EMA_SEED_TEMP    20.0f

// Minimum interval between sensor error log prints (ms)
#define SENSOR_ERROR_LOG_MS     1000

// How long (ms) the sensor can go without a valid reading before triggering ERROR
#define SENSOR_TIMEOUT_MS       5000

// Pressure transducer (GPIO PIN_PRESSURE = 1)
// Wiring: sensor OUT → R1=2.2kΩ → GPIO, R2=5.1kΩ → GND (Vgpio_max = 3.144V @ 4.5V out)
// Sensor output range: 0.5V = 0 Bar, 4.5V = PRESSURE_RANGE_BAR (ratiometric, 5V supply)
#define PRESSURE_RANGE_BAR          16.0f   // Full-scale Bar — MUST match the ordered sensor range
#define PRESSURE_DIVIDER_RATIO      0.6986f // R2/(R1+R2) = 5100/(2200+5100)
#define PRESSURE_SENSOR_V_LOW       0.5f    // Sensor output voltage at 0 Bar
#define PRESSURE_SENSOR_V_HIGH      4.5f    // Sensor output voltage at full scale
#define PRESSURE_ADC_SAMPLES        4       // Samples averaged per read to reduce ADC noise

// =====================================================================
// CONTROLS
// =====================================================================

#define SSR_WINDOW_MS               1000    // Time-proportional SSR window size (ms)
#define PID_TIMER_INTERVAL_MS       10      // Hardware timer ISR interval (ms, = 100 Hz)
#define EMERGENCY_STOP_HYSTERESIS   5.0     // °C below setpoint before emergency stop clears
#define CONTROLS_DEBUG_MS           1000    // Interval for serial PID debug output (ms)

// =====================================================================
// WEB SERVER
// =====================================================================

// WiFi credentials (station mode)
#define WIFI_SSID                   "BabaLan"
#define WIFI_PASSWORD               "bittegibmirinternet"

// Fallback AP credentials (used when station connect fails)
#define AP_SSID                     "QuickMill-PID"
#define AP_PASSWORD                 "espresso123"

#define WEBSERVER_PORT              80      // HTTP server port

#define WIFI_CONNECT_ATTEMPTS       20      // Max attempts before falling back to AP mode
#define WIFI_CONNECT_DELAY_MS       500     // Delay between connection attempts (ms)
#define WEB_BODY_READ_TIMEOUT_MS    1000    // Timeout waiting for POST body (ms)
#define WEB_SEND_BUFFER_SIZE        1024    // Chunk size for PROGMEM file transfer (bytes)

// =====================================================================
// RTOS TASKS
// =====================================================================

#define STARTUP_DELAY_MS            2000    // Boot delay before setup runs (ms)
#define TASK_DISPLAY_CYCLE_MS       2       // Display refresh interval (ms)
#define TASK_CONTROL_CYCLE_MS       100     // Control loop interval (ms, = 10 Hz)
#define TASK_WEBSERVER_CYCLE_MS     10      // Web server poll interval (ms)

#define TASK_DISPLAY_STACK          4096    // Stack size for display task (bytes)
#define TASK_CONTROL_STACK          4096    // Stack size for control task (bytes)
#define TASK_WEBSERVER_STACK        8192    // Stack size for web server task (bytes)

#define TASK_DISPLAY_PRIORITY       1       // FreeRTOS priority for display task
#define TASK_CONTROL_PRIORITY       2       // FreeRTOS priority for control task (highest)
#define TASK_WEBSERVER_PRIORITY     1       // FreeRTOS priority for web server task

// =====================================================================
// STATE MACHINE
// =====================================================================

// Safety cutoffs — trigger ERROR state, require user acknowledgment to clear
#define SAFE_TEMP_MAX               150.0f  // Max block temp before ERROR (°C)
#define SAFE_PRESSURE_MAX           14.0f   // Max pressure before ERROR (Bar)

// Brew interlock — block must cool below this before COFFEE state is allowed
#define BREW_MAX_TEMP               98.0f   // °C

// Brew substate timers (milliseconds)
#define PREINFUSE_MAX_TIME_MS       5000    // Max pre-infusion before forced transition
#define BLOOM_TIME_MS               3000    // Bloom (puck soak) duration
#define PREHEAT_TIME_MS             500     // Pre-heat burst before pump restarts
#define BREW_MAX_TIME_MS            2000    // Full-heat + pump phase to counter temp dip
#define BREW_PID_MAX_TIME_MS        45000   // Max PID brew time before DONE

// Pressure threshold to end pre-infusion early (Bar)
#define PREINFUSE_TARGET_PRESS      2.5f

// =====================================================================
// PID DEFAULTS — General
// =====================================================================

#define DEFAULT_IMAX            55.0        // Integrator clamp (max integral contribution, ms)
#define DEFAULT_EMA_FACTOR      0.6         // EMA smoothing on PID derivative input
#define DEFAULT_TARGET_TEMP     93.0        // Default setpoint (°C)

// =====================================================================
// PID DEFAULTS — Heating Mode
// =====================================================================

#define DEFAULT_KP              62.0
#define DEFAULT_KI              1.19        // = Kp / Tn  (62.0 / 52.0)
#define DEFAULT_KD              713.0       // = Tv * Kp  (11.5 * 62.0)

// =====================================================================
// PID DEFAULTS — Brew Mode
// =====================================================================

#define DEFAULT_BREW_KP             50.0
#define DEFAULT_BREW_KI             0.0     // Integral disabled during brew
#define DEFAULT_BREW_KD             1000.0  // = Tv * Kp  (20.0 * 50.0)

#endif // CONFIG_H
