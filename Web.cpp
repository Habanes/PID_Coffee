#include "Web.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(WEBSERVER_PORT);
static String    ipAddress = "0.0.0.0";

// =====================================================================
// FRONTEND  (dark espresso theme carried over verbatim from the previous
// dashboard - grain overlay, chart, duty bars, per-substate status colours -
// adapted to the NewVersion data model: 3 presets, IDLE-only writes, no
// emergency-off / switch-override rows per the safety design.)
// =====================================================================
static const char STYLE_CSS[] PROGMEM = R"CSS(
* { margin: 0; padding: 0; box-sizing: border-box; }

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, #010202 0%, #12130b 100%);
    background-attachment: fixed;
    min-height: 100vh;
    padding: 20px;
    color: #b6926e;
    position: relative;
}

/* Grain overlay - dithers the dark gradient to remove color banding */
body::before {
    content: '';
    position: fixed;
    inset: 0;
    pointer-events: none;
    z-index: 0;
    opacity: 0.055;
    background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='200' height='200'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.75' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='200' height='200' filter='url(%23n)'/%3E%3C/svg%3E");
    background-repeat: repeat;
    background-size: 200px 200px;
}

.container { position: relative; z-index: 1; max-width: 1200px; margin: 0 auto; }
.container > .card { margin-bottom: 20px; }

header {
    background: #020404;
    border: 1px solid rgba(182, 146, 110, 0.3);
    padding: 20px 30px;
    border-radius: 8px;
    margin-bottom: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.6);
}

header h1 { font-size: 2em; color: #b6926e; }

.status-badge {
    padding: 8px 20px;
    border-radius: 12px;
    font-weight: bold;
    font-size: 0.9em;
    background: #2a0f0f;
    color: #b6926e;
    border: 1px solid rgba(182, 146, 110, 0.25);
    animation: pulse 2s infinite;
}

.status-badge.connected {
    background: #103715;
    color: #b6926e;
    border-color: rgba(182, 146, 110, 0.4);
    animation: none;
}

@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }

/* Lock banner - shown while the machine is not IDLE (settings read-only) */
.lock-banner {
    background: #2a0f0f;
    border: 1px solid rgba(182, 146, 110, 0.4);
    color: #b6926e;
    padding: 12px 20px;
    border-radius: 8px;
    margin-bottom: 20px;
    text-align: center;
    font-weight: bold;
    display: none;
}
.locked input.cfg, .locked button.cfg { opacity: 0.4; pointer-events: none; }

.grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 20px;
    margin-bottom: 20px;
}

.card {
    background: #020404;
    border: 1px solid rgba(182, 146, 110, 0.2);
    padding: 25px;
    border-radius: 8px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
    transition: transform 0.3s ease, box-shadow 0.3s ease, border-color 0.3s ease;
}

.card:hover {
    transform: translateY(-4px);
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.7);
    border-color: rgba(182, 146, 110, 0.45);
}

.card-title {
    font-size: 1em;
    color: rgba(182, 146, 110, 0.6);
    margin-bottom: 15px;
    text-transform: uppercase;
    letter-spacing: 1px;
    font-weight: 600;
}

.value-display {
    display: flex;
    align-items: baseline;
    justify-content: center;
}

.value-display .value {
    font-size: 3em;
    font-weight: bold;
    color: #b6926e;
    margin-right: 10px;
}

.value-display .unit { font-size: 1.5em; color: rgba(182, 146, 110, 0.5); }

.chart-container { margin-bottom: 20px; }
.chart-container canvas { max-height: 300px; }

.duty-bar-container {
    width: 100%;
    height: 40px;
    background: rgba(182, 146, 110, 0.08);
    border: 1px solid rgba(182, 146, 110, 0.2);
    border-radius: 6px;
    overflow: hidden;
    position: relative;
}

.duty-bar {
    height: 100%;
    background: #103715;
    transition: width 0.5s ease, background-color 0.3s ease;
    display: flex;
    align-items: center;
    justify-content: center;
    min-width: 60px;
}

.duty-bar.error { background: #2a0f0f; }

.duty-bar-text {
    color: #b6926e;
    font-weight: bold;
    font-size: 1.1em;
    text-shadow: 1px 1px 2px rgba(0, 0, 0, 0.6);
}

.pid-controls {
    display: flex;
    flex-direction: column;
    gap: 20px;
}

.control-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
}

.control-group label {
    font-size: 0.9em;
    color: rgba(182, 146, 110, 0.7);
    font-weight: 600;
}

.control-group input {
    padding: 12px 15px;
    border: 1px solid rgba(182, 146, 110, 0.3);
    border-radius: 5px;
    font-size: 1.1em;
    font-weight: bold;
    color: #b6926e;
    background: #010202;
    transition: border-color 0.3s ease, box-shadow 0.3s ease;
}

.control-group input:focus {
    outline: none;
    border-color: #b6926e;
    box-shadow: 0 0 0 3px rgba(182, 146, 110, 0.1);
}

.control-group input:hover { border-color: rgba(182, 146, 110, 0.6); }

/* Style the native number spinner buttons */
.control-group input[type=number]::-webkit-inner-spin-button,
.control-group input[type=number]::-webkit-outer-spin-button {
    opacity: 1;
    background: rgba(182, 146, 110, 0.1);
    border-left: 1px solid rgba(182, 146, 110, 0.25);
    cursor: pointer;
    filter: invert(65%) sepia(30%) saturate(400%) hue-rotate(5deg) brightness(0.8);
    border-radius: 0 4px 4px 0;
    width: 18px;
}
.control-group input[type=number]::-webkit-inner-spin-button:hover,
.control-group input[type=number]::-webkit-outer-spin-button:hover {
    background: rgba(182, 146, 110, 0.22);
    filter: invert(75%) sepia(35%) saturate(450%) hue-rotate(5deg) brightness(0.9);
}

.btn-primary {
    padding: 15px 30px;
    background: linear-gradient(135deg, #103715 0%, #0a2410 100%);
    color: #b6926e;
    border: 1px solid rgba(182, 146, 110, 0.3);
    border-radius: 6px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.3s ease, border-color 0.3s ease;
    box-shadow: 0 4px 15px rgba(0, 0, 0, 0.4);
}

.btn-primary:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(16, 55, 21, 0.5);
    border-color: #b6926e;
}

.btn-primary:active {
    transform: translateY(0);
    box-shadow: 0 2px 10px rgba(16, 55, 21, 0.3);
}

.btn-primary.active {
    border-color: #b6926e;
    box-shadow: 0 0 0 3px rgba(182, 146, 110, 0.15);
}

.btn-reset {
    padding: 15px 30px;
    background: linear-gradient(135deg, #2a0f0f 0%, #1a0808 100%);
    color: #b6926e;
    border: 1px solid rgba(182, 146, 110, 0.3);
    border-radius: 6px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.3s ease, border-color 0.3s ease;
    box-shadow: 0 4px 15px rgba(0, 0, 0, 0.4);
}

.btn-reset:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(42, 15, 15, 0.5);
    border-color: #b6926e;
}

.btn-reset:active {
    transform: translateY(0);
    box-shadow: 0 2px 10px rgba(42, 15, 15, 0.3);
}

.brew-status-label { color: rgba(182, 146, 110, 0.55); font-size: 0.9em; }

/* Diagnostics card */
.diag-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
}
@media (max-width: 600px) { .diag-grid { grid-template-columns: 1fr; } }
.diag-section-title {
    font-size: 0.75em;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: rgba(182, 146, 110, 0.45);
    margin-bottom: 10px;
    border-bottom: 1px solid rgba(182, 146, 110, 0.12);
    padding-bottom: 4px;
}
.diag-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 5px 0;
    border-bottom: 1px solid rgba(182, 146, 110, 0.07);
}
.diag-label { color: rgba(182, 146, 110, 0.6); font-size: 0.88em; }
.diag-value { color: #b6926e; font-size: 0.9em; font-variant-numeric: tabular-nums; }
.diag-led {
    padding: 3px 12px;
    border-radius: 10px;
    font-size: 0.78em;
    font-weight: bold;
    letter-spacing: 0.05em;
}
.diag-led.off {
    background: rgba(182, 146, 110, 0.06);
    color: rgba(182, 146, 110, 0.35);
    border: 1px solid rgba(182, 146, 110, 0.15);
}
.diag-led.on {
    background: #261608;
    color: #b6926e;
    border: 1px solid rgba(182, 146, 110, 0.5);
    box-shadow: 0 0 6px rgba(182, 146, 110, 0.2);
}

/* Status card */
.status-display {
    font-size: 1.8em;
    font-weight: bold;
    text-align: center;
    padding: 10px 20px;
    color: #b6926e;
    border-radius: 12px;
    border: 1px solid rgba(182, 146, 110, 0.25);
    background: rgba(182, 146, 110, 0.06);
    transition: color 0.4s ease, background 0.4s ease, border-color 0.4s ease;
}

.status-display.heating   { color: #b6926e; border-color: rgba(182, 146, 110, 0.25); background: rgba(182, 146, 110, 0.06); }
.status-display.preinfuse { color: #5ab8d4; border-color: rgba(90, 184, 212, 0.4);  background: rgba(90, 184, 212, 0.08); }
.status-display.bloom     { color: #4caf6a; border-color: rgba(76, 175, 106, 0.4);  background: rgba(76, 175, 106, 0.08); }
.status-display.preheat   { color: #d4825a; border-color: rgba(212, 130, 90, 0.4);  background: rgba(212, 130, 90, 0.08); }
.status-display.brewmax   { color: #c94040; border-color: rgba(201, 64, 64, 0.4);   background: rgba(201, 64, 64, 0.08); }
.status-display.brewing   { color: #4caf6a; border-color: rgba(76, 175, 106, 0.4);  background: rgba(76, 175, 106, 0.08); }
.status-display.done      { color: #5a9ed4; border-color: rgba(90, 158, 212, 0.35); background: rgba(90, 158, 212, 0.07); }
.status-display.steam     { color: #e8e8e8; border-color: rgba(232, 232, 232, 0.35); background: rgba(232, 232, 232, 0.06); }
.status-display.emergency { color: #b84040; border-color: rgba(184, 64, 64, 0.4);   background: rgba(184, 64, 64, 0.08); animation: pulse 0.8s infinite; }

/* Buzzer / system rows */
.temp-control-row {
    display: flex;
    align-items: center;
    gap: 15px;
    flex-wrap: wrap;
}

.btn-emergency {
    padding: 15px 30px;
    background: linear-gradient(135deg, #2a0f0f 0%, #1a0808 100%);
    color: #b6926e;
    border: 1px solid rgba(182, 146, 110, 0.35);
    border-radius: 6px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    letter-spacing: 0.5px;
    transition: transform 0.2s ease, box-shadow 0.3s ease, border-color 0.3s ease;
    box-shadow: 0 4px 15px rgba(42, 15, 15, 0.5);
}

.btn-emergency:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(42, 15, 15, 0.7);
    border-color: #b6926e;
}

.btn-emergency:active { transform: translateY(0); }

.btn-emergency.forced-off {
    border-color: rgba(182, 146, 110, 0.6);
    box-shadow: 0 4px 20px rgba(42, 15, 15, 0.9);
    animation: pulse 1s infinite;
}

/* 3-column fixed grid for parameter rows */
.control-grid-3 {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 15px;
}

/* Row of action buttons */
.btn-row { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 15px; }
.btn-row > * { flex: 1; }

/* Section divider label inside cards */
.pid-section-label {
    font-size: 0.78em;
    text-transform: uppercase;
    letter-spacing: 1.5px;
    color: rgba(182, 146, 110, 0.4);
    padding-bottom: 6px;
    border-bottom: 1px solid rgba(182, 146, 110, 0.1);
}

footer {
    background: #020404;
    border: 1px solid rgba(182, 146, 110, 0.2);
    padding: 15px;
    border-radius: 8px;
    text-align: center;
    margin-top: 20px;
    color: rgba(182, 146, 110, 0.5);
}

@media (max-width: 768px) {
    header { flex-direction: column; text-align: center; gap: 15px; }
    header h1 { font-size: 1.5em; }
    .value-display .value { font-size: 2.5em; }
    .grid { grid-template-columns: 1fr; }
    .control-grid-3 { grid-template-columns: 1fr; }
    .temp-control-row { flex-direction: column; align-items: stretch; }
}

@media (hover: none) and (pointer: coarse) {
    html { font-size: 130%; }
    .value-display .value { font-size: 2.31em; }
    .value-display .unit  { font-size: 1.15em; }
    .status-display       { font-size: 1.38em; }
    .card-title           { font-size: 0.77em; }
}
)CSS";

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=1200, initial-scale=1.0">
    <title>QuickMill PID Controller</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <header>
            <h1>QuickMill Orione 3000 PID Control</h1>
            <div class="status-badge" id="statusBadge">Connecting</div>
        </header>

        <div class="lock-banner" id="lockBanner">Machine busy &mdash; settings are read-only until IDLE</div>

        <!-- Preset selection -->
        <div class="card">
            <div class="card-title">Preset</div>
            <div class="btn-row" style="margin-top:0">
                <button class="btn-primary cfg" id="preset0" onclick="selectPreset(0)">Preset 1</button>
                <button class="btn-primary cfg" id="preset1" onclick="selectPreset(1)">Preset 2</button>
                <button class="btn-primary cfg" id="preset2" onclick="selectPreset(2)">Preset 3</button>
            </div>
        </div>

        <!-- Live value cards -->
        <div class="grid">
            <div class="card">
                <div class="card-title">Sensor Temp</div>
                <div class="value-display"><span class="value" id="currentTemp">--</span><span class="unit">&deg;C</span></div>
            </div>
            <div class="card">
                <div class="card-title">Target Temp</div>
                <div class="value-display"><span class="value" id="setTemp">--</span><span class="unit">&deg;C</span></div>
            </div>
            <div class="card">
                <div class="card-title">PID Output</div>
                <div class="value-display"><span class="value" id="pidOutput">--</span><span class="unit">ms</span></div>
            </div>
            <div class="card">
                <div class="card-title">Pressure</div>
                <div class="value-display"><span class="value" id="pressureVal">--</span><span class="unit">Bar</span></div>
            </div>
            <div class="card" style="grid-column: span 2;">
                <div class="card-title">Status</div>
                <div class="status-display" id="machineStatus">--</div>
                <div style="margin-top:12px;">
                    <span class="brew-status-label" id="brewStatusLabel">Idle &mdash; activate coffee switch to start a brew</span>
                </div>
            </div>
            <div class="card" style="grid-column: span 2;">
                <div class="card-title">Set Coffee Target</div>
                <div class="control-group" style="margin-bottom:12px;">
                    <input type="number" id="coffeeTargetQuick" step="0.5" class="cfg">
                </div>
                <div style="display:flex;gap:10px;flex-wrap:wrap;">
                    <button class="btn-primary cfg" onclick="applyCoffeeTarget(this)" style="flex:1;">Set Temperature</button>
                </div>
            </div>
        </div>

        <!-- Temperature history chart -->
        <div class="chart-container">
            <div class="card">
                <div class="card-title">Temperature History</div>
                <canvas id="tempChart"></canvas>
            </div>
        </div>

        <!-- Heater output bar (real output) -->
        <div class="card">
            <div class="card-title">Heater Output</div>
            <div class="duty-bar-container">
                <div class="duty-bar" id="heaterBar" style="width: 0%;">
                    <span class="duty-bar-text">0%</span>
                </div>
            </div>
        </div>

        <!-- PID Parameters: heating row + brewing row -->
        <div class="card">
            <div class="card-title">PID Parameters</div>
            <div class="pid-controls">
                <div class="pid-section-label">Heating</div>
                <div class="control-grid-3">
                    <div class="control-group"><label for="heatingKp">Kp (Proportional)</label><input type="number" id="heatingKp" step="0.1" class="cfg"></div>
                    <div class="control-group"><label for="heatingKi">Ki (Integral)</label><input type="number" id="heatingKi" step="0.01" class="cfg"></div>
                    <div class="control-group"><label for="heatingKd">Kd (Derivative)</label><input type="number" id="heatingKd" step="1" class="cfg"></div>
                </div>
                <div class="pid-section-label" style="margin-top: 8px;">Brewing</div>
                <div class="control-grid-3">
                    <div class="control-group"><label for="brewKp">Kp (Proportional)</label><input type="number" id="brewKp" step="0.1" class="cfg"></div>
                    <div class="control-group"><label for="brewKi">Ki (Integral)</label><input type="number" id="brewKi" step="0.01" class="cfg"></div>
                    <div class="control-group"><label for="brewKd">Kd (Derivative)</label><input type="number" id="brewKd" step="1" class="cfg"></div>
                </div>
                <div class="btn-row"><button class="btn-primary cfg" onclick="applyPid(this)">Apply PID Values</button></div>
            </div>
        </div>

        <!-- Temperature & Steam -->
        <div class="card">
            <div class="card-title">Temperature &amp; Steam</div>
            <div class="control-grid-3">
                <div class="control-group"><label for="tempOffset">Sensor offset (&deg;C)</label><input type="number" id="tempOffset" step="0.5" class="cfg"></div>
                <div class="control-group"><label for="steamTarget">Steam target (&deg;C)</label><input type="number" id="steamTarget" step="0.5" class="cfg"></div>
                <div class="control-group"><label>&nbsp;</label><button class="btn-primary cfg" onclick="applyTempSteam(this)">Apply</button></div>
            </div>
        </div>

        <!-- Safety Limits -->
        <div class="card">
            <div class="card-title">Safety Limits</div>
            <div class="control-grid-3">
                <div class="control-group"><label for="coffeeTempMax">Coffee max (&deg;C)</label><input type="number" id="coffeeTempMax" step="1" class="cfg"></div>
                <div class="control-group"><label for="steamTempMax">Steam max (&deg;C)</label><input type="number" id="steamTempMax" step="1" class="cfg"></div>
                <div class="control-group"><label for="safePressureMax">Pressure max (Bar)</label><input type="number" id="safePressureMax" step="0.5" class="cfg"></div>
            </div>
            <div class="btn-row"><button class="btn-primary cfg" onclick="applySafety(this)">Apply Limits</button></div>
        </div>

        <!-- Brew Timing (active preset) -->
        <div class="card">
            <div class="card-title">Brew Timing <span style="font-size:0.72em;color:rgba(182,146,110,0.4);letter-spacing:1px;">ACTIVE PRESET</span></div>
            <div class="control-grid-3">
                <div class="control-group"><label for="preinfuse">Pre-infuse max (s)</label><input type="number" id="preinfuse" step="0.1" class="cfg"></div>
                <div class="control-group"><label for="preinfuseBar">Pre-infuse target (Bar)</label><input type="number" id="preinfuseBar" step="0.1" class="cfg"></div>
                <div class="control-group"><label for="bloom">Bloom (s)</label><input type="number" id="bloom" step="0.1" class="cfg"></div>
                <div class="control-group"><label for="preheat">Preheat (s)</label><input type="number" id="preheat" step="0.1" class="cfg"></div>
                <div class="control-group"><label for="brewMax">Brew boost (s)</label><input type="number" id="brewMax" step="0.1" class="cfg"></div>
                <div class="control-group"><label for="shot">Shot time total (s)</label><input type="number" id="shot" step="1" class="cfg"></div>
            </div>
            <div class="btn-row"><button class="btn-primary cfg" onclick="applyTiming(this)">Apply Timing</button></div>
        </div>

        <!-- Diagnostics -->
        <div class="card">
            <div class="card-title">Diagnostics</div>
            <div class="diag-grid">
                <div class="diag-section">
                    <div class="diag-section-title">Inputs</div>
                    <div class="diag-row"><span class="diag-label">Temperature</span><span class="diag-value" id="diagTemp">&mdash;</span></div>
                    <div class="diag-row"><span class="diag-label">Switch Pin Voltage</span><span class="diag-value" id="diagSwitchV">&mdash;</span></div>
                    <div class="diag-row"><span class="diag-label">Pressure Pin Voltage</span><span class="diag-value" id="diagPressureV">&mdash;</span></div>
                    <div class="diag-row"><span class="diag-label">Calculated Pressure</span><span class="diag-value" id="diagPressure">&mdash;</span></div>
                    <div class="diag-row"><span class="diag-label">Switch Steam</span><span id="diagSwSteam" class="diag-led off">OFF</span></div>
                    <div class="diag-row"><span class="diag-label">Switch Coffee</span><span id="diagSwCoffee" class="diag-led off">OFF</span></div>
                    <div class="diag-row"><span class="diag-label">Temp Sensor</span><span id="diagTempOk" class="diag-led on">OK</span></div>
                </div>
                <div class="diag-section">
                    <div class="diag-section-title">Outputs</div>
                    <div class="diag-row"><span class="diag-label">Heater Mode</span><span class="diag-value" id="diagHeater">&mdash;</span></div>
                    <div class="diag-row"><span class="diag-label">Pump</span><span id="diagPump" class="diag-led off">OFF</span></div>
                    <div class="diag-row"><span class="diag-label">Valve</span><span id="diagValve" class="diag-led off">OFF</span></div>
                    <div class="diag-row"><span class="diag-label">Brew Timer</span><span class="diag-value" id="diagTimer">&mdash;</span></div>
                </div>
            </div>
        </div>

        <!-- System -->
        <div class="card">
            <div class="card-title">System</div>
            <div class="btn-row" style="margin-top:0">
                <button class="btn-emergency cfg" id="muteBtn" onclick="toggleMute()">Mute</button>
                <button class="btn-reset cfg" onclick="resetAll()">Reset All Settings</button>
            </div>
        </div>

        <footer>
            <p>ESP32-S3 PID | CoffeePID</p>
        </footer>
    </div>

    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="/script.js"></script>
</body>
</html>
)HTML";

static const char SCRIPT_JS[] PROGMEM = R"JS(
const $ = id => document.getElementById(id);
let idle = true, chart = null;
const MAX_DATA_POINTS = 200;

// ---- Temperature history chart ----
if (typeof Chart !== 'undefined') {
    if (window.matchMedia('(hover: none) and (pointer: coarse)').matches) Chart.defaults.font.size = 14;
    chart = new Chart($('tempChart').getContext('2d'), {
        type: 'line',
        data: { labels: [], datasets: [
            { label: 'Current Temp (°C)', data: [], borderColor: '#103715',
              backgroundColor: 'rgba(16, 55, 21, 0.15)', borderWidth: 2, tension: 0.4, fill: true, pointRadius: 0 },
            { label: 'Target Temp (°C)', data: [], borderColor: '#b84040',
              backgroundColor: 'transparent', borderWidth: 1.5, borderDash: [5, 5], tension: 0, fill: false, pointRadius: 0 },
        ]},
        options: {
            responsive: true, maintainAspectRatio: true,
            plugins: {
                legend: { display: true, position: 'top', labels: { color: 'rgba(182, 146, 110, 0.75)', boxWidth: 16 } },
                tooltip: { mode: 'index', intersect: false },
            },
            scales: {
                x: { display: true, grid: { color: 'rgba(182, 146, 110, 0.07)' },
                     ticks: { color: 'rgba(182, 146, 110, 0.5)', maxTicksLimit: 10 } },
                y: { display: true, grid: { color: 'rgba(182, 146, 110, 0.07)' },
                     ticks: { color: 'rgba(182, 146, 110, 0.5)', callback: v => v + '°C' } },
            },
            interaction: { mode: 'nearest', axis: 'x', intersect: false },
        },
    });
} else { console.error('Chart.js not loaded!'); }

function pushChart(cur, set) {
    if (!chart) return;
    chart.data.labels.push(new Date().toLocaleTimeString());
    chart.data.datasets[0].data.push(cur);
    chart.data.datasets[1].data.push(set);
    if (chart.data.labels.length > MAX_DATA_POINTS) {
        chart.data.labels.shift(); chart.data.datasets[0].data.shift(); chart.data.datasets[1].data.shift();
    }
    const all = [...chart.data.datasets[0].data, ...chart.data.datasets[1].data];
    if (all.length) {
        const mn = Math.min(...all), mx = Math.max(...all), m = Math.max((mx - mn) * 0.1, 0.5);
        chart.options.scales.y.min = Math.floor(mn - m);
        chart.options.scales.y.max = Math.ceil(mx + m);
    }
    chart.update('none');
}

// ---- Settings ----
async function loadSettings() {
    const s = await (await fetch('/api/settings')).json();
    $('heatingKp').value = s.heatingKp; $('heatingKi').value = s.heatingKi; $('heatingKd').value = s.heatingKd;
    $('brewKp').value = s.brewKp; $('brewKi').value = s.brewKi; $('brewKd').value = s.brewKd;
    $('tempOffset').value = s.tempOffset; $('steamTarget').value = s.steamTarget;
    $('coffeeTempMax').value = s.coffeeTempMax; $('steamTempMax').value = s.steamTempMax; $('safePressureMax').value = s.safePressureMax;
    $('coffeeTargetQuick').value = s.coffeeTarget;
    $('preinfuse').value = (s.preinfuseMs / 1000).toFixed(1); $('preinfuseBar').value = s.preinfuseBar;
    $('bloom').value = (s.bloomMs / 1000).toFixed(1); $('preheat').value = (s.preheatMs / 1000).toFixed(1);
    $('brewMax').value = (s.brewMaxMs / 1000).toFixed(1); $('shot').value = Math.round(s.shotMs / 1000);
    for (let i = 0; i < 3; i++) $('preset' + i).classList.toggle('active', i === s.activePreset);
    updateMuteBtn(s.buzzerMute);
}

function flash(btn) {
    if (!btn) return;
    const t = btn.textContent;
    btn.textContent = 'Applied ✓';
    setTimeout(() => { btn.textContent = t; }, 1500);
}

async function post(obj, btn) {
    const r = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(obj) });
    if (r.status === 409) { alert('Machine busy — settings are read-only until IDLE'); return; }
    flash(btn);
    loadSettings();
}

function applyPid(btn) {
    post({ heatingKp: +$('heatingKp').value, heatingKi: +$('heatingKi').value, heatingKd: +$('heatingKd').value,
           brewKp: +$('brewKp').value, brewKi: +$('brewKi').value, brewKd: +$('brewKd').value }, btn);
}
function applyCoffeeTarget(btn) { post({ coffeeTarget: +$('coffeeTargetQuick').value }, btn); }
function applyTempSteam(btn) { post({ tempOffset: +$('tempOffset').value, steamTarget: +$('steamTarget').value }, btn); }
function applySafety(btn) { post({ coffeeTempMax: +$('coffeeTempMax').value, steamTempMax: +$('steamTempMax').value, safePressureMax: +$('safePressureMax').value }, btn); }
function applyTiming(btn) {
    post({ preinfuseMs: Math.round($('preinfuse').value * 1000),
           preinfuseBar: +$('preinfuseBar').value, bloomMs: Math.round($('bloom').value * 1000),
           preheatMs: Math.round($('preheat').value * 1000), brewMaxMs: Math.round($('brewMax').value * 1000),
           shotMs: Math.round($('shot').value * 1000) }, btn);
}
async function selectPreset(i) {
    const r = await fetch('/api/preset', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ index: i }) });
    if (r.status === 409) { alert('Machine busy — settings are read-only until IDLE'); return; }
    loadSettings();
}
function updateMuteBtn(m) {
    const b = $('muteBtn');
    if (m) { b.classList.add('forced-off'); b.textContent = 'Mute [ON]'; }
    else   { b.classList.remove('forced-off'); b.textContent = 'Mute'; }
}
function toggleMute() { post({ buzzerMute: !$('muteBtn').classList.contains('forced-off') }); }
async function resetAll() {
    if (!confirm('Reset all settings to defaults?')) return;
    const r = await fetch('/api/reset', { method: 'POST' });
    if (r.status === 409) { alert('Machine busy — settings are read-only until IDLE'); return; }
    loadSettings();
}

// ---- Live state ----
function led(id, on, onText, offText) {
    const el = $(id); if (!el) return;
    el.className = 'diag-led ' + (on ? 'on' : 'off');
    el.textContent = on ? (onText || 'ON') : (offText || 'OFF');
}

function updateMachineStatus(ms, cs) {
    const el = $('machineStatus'); if (!el) return;
    if (ms === 'ERROR') { el.textContent = 'ERROR'; el.className = 'status-display emergency'; }
    else if (ms === 'COFFEE') {
        const map = { 'PREINFUSE': ['Pre-Infuse', 'preinfuse'], 'BLOOM': ['Bloom', 'bloom'], 'PREHEAT': ['Preheat', 'preheat'],
                      'BREW_MAX': ['Brew Boost', 'brewmax'], 'BREW_PID': ['Brewing', 'brewing'], 'DONE': ['Done', 'done'] };
        const [label, cls] = map[cs] || ['Brewing', 'brewing'];
        el.textContent = label; el.className = 'status-display ' + cls;
    }
    else if (ms === 'STEAM') { el.textContent = 'Steam'; el.className = 'status-display steam'; }
    else { el.textContent = 'Heating Up'; el.className = 'status-display heating'; }
}

function updateBrewStatus(ms, cs, err) {
    const label = $('brewStatusLabel'); if (!label) return;
    const labels = {
        'PREINFUSE': 'Pump on — building pressure through puck',
        'BLOOM':     'Pump off — soaking puck, valve holds pressure',
        'PREHEAT':   'Full heat burst — recovering block temperature',
        'BREW_MAX':  'Full heat + pump — countering temperature dip',
        'BREW_PID':  'Brew PID active — maintaining extraction temperature',
        'DONE':      'Shot complete — release coffee switch to return to idle',
    };
    if (ms === 'COFFEE' && labels[cs]) label.textContent = labels[cs];
    else if (ms === 'STEAM') label.textContent = 'Steam mode — heating block to steam target';
    else if (ms === 'ERROR') label.textContent = err
        ? '⚠ ' + err + ' — turn both switches off to acknowledge'
        : 'Safety lockout — turn both switches off to acknowledge';
    else label.textContent = 'Idle — activate coffee switch to start a brew';
}

function updateBars(dutyCycle, heaterMode) {
    const actual = heaterMode === 'FULL' ? 100.0 : heaterMode === 'OFF' ? 0.0 : dutyCycle;
    const hb = $('heaterBar');
    hb.style.width = actual + '%';
    hb.style.background = heaterMode === 'FULL' ? '#2a1508' : '#103715';
    hb.querySelector('.duty-bar-text').textContent =
        heaterMode === 'FULL' ? '100%  ▲ FULL ON' :
        heaterMode === 'OFF'  ? '0%  OFF' :
        actual.toFixed(1) + '%';
}

function updateDiag(d) {
    function set(id, v) { const el = $(id); if (el) el.textContent = v; }
    set('diagTemp',     d.currentTemp.toFixed(1) + ' °C');
    set('diagSwitchV',  d.switchV.toFixed(3) + ' V');
    set('diagPressureV',d.pressureV.toFixed(3) + ' V');
    set('diagPressure', d.pressure.toFixed(2) + ' Bar');
    led('diagSwSteam',  d.swSteam);
    led('diagSwCoffee', d.swCoffee);
    led('diagTempOk',   !d.tempErr, 'OK', 'FAULT');
    set('diagHeater',   d.heaterMode);
    led('diagPump',     d.pumpOn);
    led('diagValve',    d.valveOn);
    set('diagTimer',    (d.brewTimer / 1000).toFixed(1) + ' s');
}

async function poll() {
    try {
        const d = await (await fetch('/api/state')).json();
        $('currentTemp').textContent = d.currentTemp.toFixed(1);
        $('setTemp').textContent     = d.setTemp.toFixed(1);
        $('pidOutput').textContent   = Math.round(d.pidOutput);
        $('pressureVal').textContent = d.pressure.toFixed(2);
        updateMachineStatus(d.machineState, d.coffeeSubstate);
        updateBrewStatus(d.machineState, d.coffeeSubstate, d.errorReason);
        updateBars(d.dutyCycle, d.heaterMode);
        updateDiag(d);
        pushChart(d.currentTemp, d.setTemp);
        const b = $('statusBadge'); b.textContent = 'Connected'; b.classList.add('connected');
        idle = (d.machineState === 'IDLE');
        $('lockBanner').style.display = idle ? 'none' : 'block';
        document.body.classList.toggle('locked', !idle);
    } catch (e) {
        const b = $('statusBadge'); b.textContent = 'Disconnected'; b.classList.remove('connected');
    }
}

loadSettings();
poll();
setInterval(poll, 1000);
)JS";

// =====================================================================
// JSON helpers (flat bodies only)
// =====================================================================
static float jget(const String& b, const char* key, float dflt) {
    String k = String("\"") + key + "\"";
    int i = b.indexOf(k); if (i < 0) return dflt;
    i = b.indexOf(':', i); if (i < 0) return dflt;
    return b.substring(i + 1).toFloat();
}
static bool jgetb(const String& b, const char* key, bool dflt) {
    String k = String("\"") + key + "\"";
    int i = b.indexOf(k); if (i < 0) return dflt;
    i = b.indexOf(':', i); if (i < 0) return dflt;
    String rest = b.substring(i + 1); rest.trim();
    return rest.startsWith("true");
}

static const char* heaterModeText(HeaterMode m) {
    switch (m) { case HEATER_OFF: return "OFF"; case HEATER_FULL_ON: return "FULL"; default: return "PID"; }
}

// Uppercase enum strings (consumed by the dashboard JS for status colouring).
static const char* machineEnumText(MachineState s) {
    switch (s) { case STATE_COFFEE: return "COFFEE"; case STATE_STEAM: return "STEAM";
                 case STATE_ERROR: return "ERROR"; default: return "IDLE"; }
}
static const char* substateEnumText(CoffeeSubstate s) {
    switch (s) { case SUB_PREINFUSE: return "PREINFUSE"; case SUB_BLOOM: return "BLOOM";
                 case SUB_PREHEAT: return "PREHEAT"; case SUB_BREW_MAX: return "BREW_MAX";
                 case SUB_BREW_PID: return "BREW_PID"; case SUB_DONE: return "DONE"; default: return "NONE"; }
}

// =====================================================================
// Handlers
// =====================================================================
static void handleIndex() { server.send_P(200, "text/html", INDEX_HTML); }
static void handleCss()   { server.send_P(200, "text/css",  STYLE_CSS); }
static void handleJs()    { server.send_P(200, "application/javascript", SCRIPT_JS); }

static void handleState() {
    SystemState s = stateSnapshot();
    double duty    = s.heatingPidOutput;                       // 0..SSR_WINDOW_MS (ON time, ms)
    double dutyPct = duty / (double)SSR_WINDOW_MS * 100.0;     // 0..100 %
    String j = "{";
    j += "\"currentTemp\":"  + String(s.currentTemperature, 1);
    j += ",\"setTemp\":"     + String(s.currentTargetTemperature, 1);
    j += ",\"pidOutput\":"   + String((int)duty);
    j += ",\"dutyCycle\":"   + String(dutyPct, 1);
    j += ",\"pressure\":"    + String(s.currentPressure, 2);
    j += ",\"pressureV\":"   + String(s.pressureVoltage, 3);
    j += ",\"switchV\":"     + String(s.switchVoltage, 3);
    j += ",\"swSteam\":"     + String(s.switchSteam ? "true" : "false");
    j += ",\"swCoffee\":"    + String(s.switchCoffee ? "true" : "false");
    j += ",\"pumpOn\":"      + String(s.pumpState ? "true" : "false");
    j += ",\"valveOn\":"     + String(s.valveState ? "true" : "false");
    j += ",\"tempErr\":"     + String(s.temperatureSensorError ? "true" : "false");
    j += ",\"brewTimer\":"   + String(s.brewTimerElapsedMs);
    j += ",\"machineState\":\""   + String(machineEnumText(s.machineState)) + "\"";
    j += ",\"coffeeSubstate\":\""  + String(substateEnumText(s.coffeeSubstate)) + "\"";
    j += ",\"errorReason\":\""     + String(errorReasonText(s.errorReason)) + "\"";
    j += ",\"heaterMode\":\""       + String(heaterModeText(s.heaterMode)) + "\"";
    j += "}";
    server.send(200, "application/json", j);
}

static void handleGetSettings() {
    Settings s = settingsSnapshot();
    const Preset& p = s.preset[s.activePresetIndex];
    String j = "{";
    j += "\"heatingKp\":" + String(s.heatingKp, 2) + ",\"heatingKi\":" + String(s.heatingKi, 3) + ",\"heatingKd\":" + String(s.heatingKd, 1);
    j += ",\"brewKp\":" + String(s.brewKp, 2) + ",\"brewKi\":" + String(s.brewKi, 3) + ",\"brewKd\":" + String(s.brewKd, 1);
    j += ",\"tempOffset\":" + String(s.tempOffset, 1);
    j += ",\"buzzerMute\":" + String(s.buzzerMute ? "true" : "false");
    j += ",\"coffeeTempMax\":" + String(s.coffeeTempMax, 0) + ",\"steamTempMax\":" + String(s.steamTempMax, 0);
    j += ",\"safePressureMax\":" + String(s.safePressureMax, 1);
    j += ",\"activePreset\":" + String(s.activePresetIndex);
    j += ",\"coffeeTarget\":" + String(p.coffeeTargetTemp, 1) + ",\"steamTarget\":" + String(p.steamTargetTemp, 1);
    j += ",\"preinfuseMs\":" + String(p.preinfuseMaxMs) + ",\"bloomMs\":" + String(p.bloomMs);
    j += ",\"preheatMs\":" + String(p.preheatMs) + ",\"brewMaxMs\":" + String(p.brewMaxMs);
    j += ",\"shotMs\":" + String(p.shotMs) + ",\"preinfuseBar\":" + String(p.preinfuseTargetBar, 1);
    j += "}";
    server.send(200, "application/json", j);
}

static bool idleNow() { return stateSnapshot().machineState == STATE_IDLE; }

static void handlePostSettings() {
    if (!idleNow()) { server.send(409, "text/plain", "busy"); return; }
    String b = server.arg("plain");

    Settings s = settingsSnapshot();
    s.heatingKp = jget(b, "heatingKp", s.heatingKp);
    s.heatingKi = jget(b, "heatingKi", s.heatingKi);
    s.heatingKd = jget(b, "heatingKd", s.heatingKd);
    s.brewKp    = jget(b, "brewKp", s.brewKp);
    s.brewKi    = jget(b, "brewKi", s.brewKi);
    s.brewKd    = jget(b, "brewKd", s.brewKd);
    s.tempOffset      = jget(b, "tempOffset", s.tempOffset);
    s.coffeeTempMax   = jget(b, "coffeeTempMax", s.coffeeTempMax);
    s.steamTempMax    = jget(b, "steamTempMax", s.steamTempMax);
    s.safePressureMax = jget(b, "safePressureMax", s.safePressureMax);
    s.buzzerMute      = jgetb(b, "buzzerMute", s.buzzerMute);

    Preset& p = s.preset[s.activePresetIndex];
    p.coffeeTargetTemp   = jget(b, "coffeeTarget", p.coffeeTargetTemp);
    p.steamTargetTemp    = jget(b, "steamTarget", p.steamTargetTemp);
    p.preinfuseMaxMs     = (uint32_t)jget(b, "preinfuseMs", p.preinfuseMaxMs);
    p.bloomMs            = (uint32_t)jget(b, "bloomMs", p.bloomMs);
    p.preheatMs          = (uint32_t)jget(b, "preheatMs", p.preheatMs);
    p.brewMaxMs          = (uint32_t)jget(b, "brewMaxMs", p.brewMaxMs);
    p.shotMs             = (uint32_t)jget(b, "shotMs", p.shotMs);
    p.preinfuseTargetBar = jget(b, "preinfuseBar", p.preinfuseTargetBar);

    settingsApply(s);   // clamps + persists
    server.send(200, "text/plain", "ok");
}

static void handlePostPreset() {
    if (!idleNow()) { server.send(409, "text/plain", "busy"); return; }
    String b = server.arg("plain");
    int idx = (int)jget(b, "index", 0);
    settingsSetActivePreset((uint8_t)idx);
    server.send(200, "text/plain", "ok");
}

static void handlePostReset() {
    if (!idleNow()) { server.send(409, "text/plain", "busy"); return; }
    resetSettings();
    server.send(200, "text/plain", "ok");
}

// =====================================================================
// WiFi + task
// =====================================================================
static void setupWeb() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_ATTEMPTS) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_DELAY_MS));
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        ipAddress = WiFi.localIP().toString();
        Serial.printf("[WEB] Connected, IP %s\n", ipAddress.c_str());
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        ipAddress = WiFi.softAPIP().toString();
        Serial.printf("[WEB] AP mode, IP %s\n", ipAddress.c_str());
    }

    server.on("/",            HTTP_GET,  handleIndex);
    server.on("/style.css",   HTTP_GET,  handleCss);
    server.on("/script.js",   HTTP_GET,  handleJs);
    server.on("/api/state",   HTTP_GET,  handleState);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/preset",  HTTP_POST, handlePostPreset);
    server.on("/api/reset",   HTTP_POST, handlePostReset);
    server.begin();
}

static void webTask(void* pv) {
    setupWeb();
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(TASK_WEB_CYCLE_MS));
    }
}

void startWebTask() {
    xTaskCreatePinnedToCore(webTask, "Web", TASK_WEB_STACK, NULL, TASK_WEB_PRIORITY, NULL, TASK_WEB_CORE);
}

String webGetIp() { return ipAddress; }
