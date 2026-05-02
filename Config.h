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

// Brew button (active LOW, internal pull-up)
#define PIN_BREW                2

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
#define DEFAULT_BREW_BOOST_SECONDS  5       // Seconds heater runs at boost duty cycle after brew starts
#define DEFAULT_BREW_BOOST_DUTY_CYCLE 100   // % duty cycle during boost window (100 = full on)
#define DEFAULT_BREW_DELAY_SECONDS  5       // Seconds at delay duty cycle after the boost window
#define DEFAULT_BREW_DELAY_DUTY_CYCLE 0     // % duty cycle during delay window (0 = full off)

#endif // CONFIG_H
