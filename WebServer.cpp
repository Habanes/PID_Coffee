/**
 * @file WebServer.cpp
 * @brief WiFi web server serving static dashboard files
 */

#include "WebServer.h"
#include "State.h"
#include "Controls.h"
#include "Buzzer.h"
#include <WiFi.h>

// Web Server
WiFiServer server(WEBSERVER_PORT);

// HTML content (stored in flash memory to save RAM)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>QuickMill PID Controller</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <header>
            <h1>QuickMill Orione 3000 PID Control</h1>
            <div class="status-badge" id="statusBadge">Disconnected</div>
        </header>

        <!-- Live value cards -->
        <div class="grid">
            <div class="card">
                <div class="card-title">Current Temperature</div>
                <div class="value-display" id="currentTemp">
                    <span class="value">92.5</span>
                    <span class="unit">°C</span>
                </div>
            </div>
            <div class="card">
                <div class="card-title">Set Temperature</div>
                <div class="value-display" id="setTemp">
                    <span class="value">93.0</span>
                    <span class="unit">°C</span>
                </div>
            </div>
            <div class="card">
                <div class="card-title">PID Output</div>
                <div class="value-display" id="pidOutput">
                    <span class="value">450</span>
                    <span class="unit">ms</span>
                </div>
            </div>
            <div class="card status-card" id="statusCard">
                <div class="card-title">Status</div>
                <div class="status-display" id="machineStatus">Heating Up</div>
            </div>
        </div>

        <!-- Temperature history chart -->
        <div class="chart-container">
            <div class="card">
                <div class="card-title">Temperature History</div>
                <canvas id="tempChart"></canvas>
            </div>
        </div>

        <!-- Duty cycle bar -->
        <div class="card">
            <div class="card-title">Relay Duty Cycle</div>
            <div class="duty-bar-container">
                <div class="duty-bar" id="dutyBar" style="width: 45%;">
                    <span class="duty-bar-text">45%</span>
                </div>
            </div>
        </div>

        <!-- Temperature set + Emergency Off -->
        <div class="card">
            <div class="card-title">Temperature Control</div>
            <div class="temp-control-row">
                <div class="control-group" style="flex: 1; min-width: 180px;">
                    <label for="targetInput">Target Temperature (°C)</label>
                    <input type="number" id="targetInput" step="0.5" value="93.0" min="0" max="120">
                </div>
                <button class="btn-primary" onclick="applyTargetTemp(this)">Set Temperature</button>
                <button class="btn-emergency" id="relayForceOffBtn" onclick="toggleRelayForceOff()">Emergency Off</button>
            </div>
        </div>

        <!-- PID Parameters: heating row + brewing row -->
        <div class="card">
            <div class="card-title">PID Parameters</div>
            <div class="pid-controls">
                <div class="pid-section-label">Heating</div>
                <div class="control-grid-3">
                    <div class="control-group">
                        <label for="kpInput">Kp (Proportional)</label>
                        <input type="number" id="kpInput" step="0.1" value="75.0" min="0" max="500">
                    </div>
                    <div class="control-group">
                        <label for="kiInput">Ki (Integral)</label>
                        <input type="number" id="kiInput" step="0.1" value="1.5" min="0" max="50">
                    </div>
                    <div class="control-group">
                        <label for="kdInput">Kd (Derivative)</label>
                        <input type="number" id="kdInput" step="0.1" value="0.0" min="0" max="2000">
                    </div>
                </div>

                <div class="pid-section-label" style="margin-top: 8px;">Brewing</div>
                <div class="control-grid-3">
                    <div class="control-group">
                        <label for="brewKpInput">Kp (Proportional)</label>
                        <input type="number" id="brewKpInput" step="0.1" value="50.0" min="0" max="500">
                    </div>
                    <div class="control-group">
                        <label for="brewKiInput">Ki (Integral)</label>
                        <input type="number" id="brewKiInput" step="0.1" value="0.5" min="0" max="50">
                    </div>
                    <div class="control-group">
                        <label for="brewKdInput">Kd (Derivative)</label>
                        <input type="number" id="brewKdInput" step="0.1" value="8.0" min="0" max="2000">
                    </div>
                </div>

                <div style="display: flex; gap: 10px; flex-wrap: wrap;">
                    <button class="btn-primary" onclick="applyAllPID(this)" style="flex: 1;">Apply PID Values</button>
                    <button class="btn-reset" onclick="resetAllPID(this)" style="flex: 1;">Reset PID Values</button>
                    <button class="btn-reset" onclick="resetPIDMemory(this)" style="flex: 1; background: linear-gradient(135deg, #1a1a10 0%, #0f0f08 100%);">Reset PID Memory</button>
                </div>
            </div>
        </div>

        <!-- Brew timing -->
        <div class="card">
            <div class="card-title">Brew Timing</div>
            <div class="brew-status-row">
                <span class="brew-status-indicator" id="brewStatusBadge">INACTIVE</span>
                <span class="brew-status-label" id="brewStatusLabel">Brew mode off &mdash; press GPIO&nbsp;0 button to activate</span>
            </div>
            <div class="pid-controls">
                <div class="control-grid">
                    <div class="control-group">
                        <label for="brewBoostSecondsInput">Boost Duration (s)</label>
                        <input type="number" id="brewBoostSecondsInput" step="1" value="5" min="0" max="30">
                    </div>
                    <div class="control-group">
                        <label for="brewBoostDutyInput">Boost Duty Cycle (%)</label>
                        <input type="number" id="brewBoostDutyInput" step="1" value="100" min="0" max="100">
                    </div>
                    <div class="control-group">
                        <label for="brewDelaySecondsInput">Delay Duration (s)</label>
                        <input type="number" id="brewDelaySecondsInput" step="1" value="5" min="0" max="30">
                    </div>
                    <div class="control-group">
                        <label for="brewDelayDutyInput">Delay Duty Cycle (%)</label>
                        <input type="number" id="brewDelayDutyInput" step="1" value="0" min="0" max="100">
                    </div>
                </div>
                <div style="display: flex; gap: 10px;">
                    <button class="btn-brew" onclick="applyBrewSettings(this)" style="flex: 1;">Apply Timing</button>
                    <button class="btn-reset" onclick="resetBrewSettings(this)" style="flex: 1;">Reset Defaults</button>
                </div>
            </div>
        </div>

        <!-- Buzzer -->
        <div class="card">
            <div class="card-title">Buzzer</div>
            <div class="temp-control-row">
                <span style="flex:1; color:#b6926e;">Mute all buzzer sounds</span>
                <button class="btn-emergency" id="buzzerMuteBtn" onclick="toggleBuzzerMute()">Mute</button>
            </div>
        </div>

        <footer>
            <p>ESP32-S3 PID | Habanes</p>
        </footer>
    </div>

    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="/script.js"></script>
</body>
</html>
)rawliteral";

const char style_css[] PROGMEM = R"rawliteral(
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

/* Grain overlay — dithers the dark gradient to remove color banding */
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

.control-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 15px;
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

.btn-brew {
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

.btn-brew:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(16, 55, 21, 0.5);
    border-color: #b6926e;
}

.btn-brew:active {
    transform: translateY(0);
    box-shadow: 0 2px 10px rgba(16, 55, 21, 0.3);
}

.btn-relay-override {
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

.btn-relay-override:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(16, 55, 21, 0.5);
    border-color: #b6926e;
}

.btn-relay-override.forced-off {
    background: linear-gradient(135deg, #2a0f0f 0%, #1a0808 100%);
    box-shadow: 0 4px 15px rgba(42, 15, 15, 0.4);
}

.btn-relay-override.forced-off:hover {
    box-shadow: 0 6px 20px rgba(42, 15, 15, 0.6);
    border-color: #b6926e;
}

.brew-status-row {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 20px;
}

.brew-status-indicator {
    padding: 6px 16px;
    border-radius: 12px;
    font-weight: bold;
    font-size: 0.85em;
    background: rgba(182, 146, 110, 0.08);
    color: rgba(182, 146, 110, 0.45);
    border: 1px solid rgba(182, 146, 110, 0.18);
    transition: all 0.3s ease;
    white-space: nowrap;
}
.brew-status-indicator.active {
    background: #261608;
    color: #b6926e;
    border-color: rgba(182, 146, 110, 0.5);
    animation: pulse 1s infinite;
}
.brew-status-indicator.boost {
    background: #2a0f0f;
    color: #b6926e;
    border-color: rgba(182, 146, 110, 0.5);
    animation: pulse 0.5s infinite;
}
.brew-status-indicator.delay {
    background: #080f1a;
    color: #b6926e;
    border-color: rgba(182, 146, 110, 0.5);
    animation: pulse 0.5s infinite;
}
.brew-status-label { color: rgba(182, 146, 110, 0.55); font-size: 0.9em; }

/* Status card */
.status-display {
    font-size: 1.8em;
    font-weight: bold;
    text-align: center;
    padding: 10px 0;
    color: #b6926e;
    transition: color 0.4s ease;
}

.status-display.heating  { color: #b6926e; }
.status-display.brewing  { color: #4caf6a; }
.status-display.emergency { color: #b84040; animation: pulse 0.8s infinite; }

/* Temperature control row */
.temp-control-row {
    display: flex;
    align-items: flex-end;
    gap: 15px;
    flex-wrap: wrap;
}

/* Emergency Off button — always red */
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

/* 3-column fixed grid for PID parameter rows */
.control-grid-3 {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 15px;
}

/* Section divider label inside PID card */
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
)rawliteral";

const char script_js[] PROGMEM = R"rawliteral(
let currentTemp = 92.5, setTemp = 93.0, pidOutput = 450, dutyCycle = 45.0;
if (typeof Chart === 'undefined') { console.error('Chart.js not loaded!'); }
const ctx = document.getElementById('tempChart').getContext('2d');
const chart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Current Temp (\u00b0C)', data: [], borderColor: '#103715',
            backgroundColor: 'rgba(16, 55, 21, 0.15)', borderWidth: 2,
            tension: 0.4, fill: true, pointRadius: 0,
        }, {
            label: 'Set Temp (\u00b0C)', data: [], borderColor: '#b84040',
            backgroundColor: 'transparent', borderWidth: 1.5,
            borderDash: [5, 5], tension: 0, fill: false, pointRadius: 0,
        }],
    },
    options: {
        responsive: true, maintainAspectRatio: true,
        plugins: {
            legend: {
                display: true, position: 'top',
                labels: { color: 'rgba(182, 146, 110, 0.75)', boxWidth: 16 },
            },
            tooltip: { mode: 'index', intersect: false },
        },
        scales: {
            x: {
                display: true,
                grid: { color: 'rgba(182, 146, 110, 0.07)' },
                ticks: { color: 'rgba(182, 146, 110, 0.5)', maxTicksLimit: 10 },
            },
            y: {
                display: true,
                grid: { color: 'rgba(182, 146, 110, 0.07)' },
                ticks: { color: 'rgba(182, 146, 110, 0.5)', callback: v => v + '\u00b0C' },
            },
        },
        interaction: { mode: 'nearest', axis: 'x', intersect: false },
    },
});
const MAX_DATA_POINTS = 200;
function updateDashboard() {
    document.querySelector('#currentTemp .value').textContent = currentTemp.toFixed(1);
    document.querySelector('#setTemp .value').textContent = setTemp.toFixed(1);
    document.querySelector('#pidOutput .value').textContent = Math.round(pidOutput);
    const dutyBar = document.getElementById('dutyBar');
    dutyBar.style.width = dutyCycle + '%';
    dutyBar.querySelector('.duty-bar-text').textContent = dutyCycle.toFixed(1) + '%';
    const timestamp = new Date().toLocaleTimeString();
    chart.data.labels.push(timestamp);
    chart.data.datasets[0].data.push(currentTemp);
    chart.data.datasets[1].data.push(setTemp);
    if (chart.data.labels.length > MAX_DATA_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
        chart.data.datasets[1].data.shift();
    }
    if (chart.data.datasets[0].data.length > 0) {
        const allTemps = [...chart.data.datasets[0].data, ...chart.data.datasets[1].data];
        const minTemp = Math.min(...allTemps);
        const maxTemp = Math.max(...allTemps);
        const range = maxTemp - minTemp;
        const margin = Math.max(range * 0.1, 0.5);
        chart.options.scales.y.min = Math.floor(minTemp - margin);
        chart.options.scales.y.max = Math.ceil(maxTemp + margin);
    }
    chart.update('none');
}
function fetchRealData() {
    fetch('/api/data').then(r => r.json()).then(data => {
        currentTemp = data.currentTemp; setTemp = data.setTemp;
        pidOutput = data.pidOutput; dutyCycle = data.dutyCycle;
        dashboardAPI.setConnectionStatus(true);
        dashboardAPI.setRelayError(data.error);
        updateBrewStatus(data.brewMode, data.brewBoostPhase, data.brewDelayPhase);
        updateRelayForceBtn(data.relayForceOff);
        updateMachineStatus(data.relayForceOff, data.brewMode);
        updateDashboard();
    }).catch(e => {
        console.error('Fetch error:', e);
        dashboardAPI.setConnectionStatus(false);
    });
}
function updateMachineStatus(emergencyOff, brewMode) {
    const el = document.getElementById('machineStatus');
    if (!el) return;
    if (emergencyOff) {
        el.textContent = 'Emergency Off';
        el.className = 'status-display emergency';
    } else if (brewMode) {
        el.textContent = 'Brewing';
        el.className = 'status-display brewing';
    } else {
        el.textContent = 'Heating Up';
        el.className = 'status-display heating';
    }
}
function updateBrewStatus(brewMode, boostPhase, delayPhase) {
    const badge = document.getElementById('brewStatusBadge');
    const label = document.getElementById('brewStatusLabel');
    if (!badge || !label) return;
    if (brewMode && boostPhase) {
        badge.textContent = 'BOOST'; badge.className = 'brew-status-indicator boost';
        label.textContent = 'Brew boost \u2014 heater at full power';
    } else if (brewMode && delayPhase) {
        badge.textContent = 'DELAY'; badge.className = 'brew-status-indicator delay';
        label.textContent = 'Pre-brew delay \u2014 heater off, brew PID pending';
    } else if (brewMode) {
        badge.textContent = 'BREWING'; badge.className = 'brew-status-indicator active';
        label.textContent = 'Brew PID active \u2014 maintaining extraction temperature';
    } else {
        badge.textContent = 'INACTIVE'; badge.className = 'brew-status-indicator';
        label.textContent = 'Brew mode off \u2014 press GPIO\u00a00 button to activate';
    }
}
function updateRelayForceBtn(forceOff) {
    const btn = document.getElementById('relayForceOffBtn');
    if (!btn) return;
    if (forceOff) {
        btn.classList.add('forced-off');
        btn.textContent = 'Emergency Off [ACTIVE]';
    } else {
        btn.classList.remove('forced-off');
        btn.textContent = 'Emergency Off';
    }
}
function updateBuzzerMuteBtn(muted) {
    const btn = document.getElementById('buzzerMuteBtn');
    if (!btn) return;
    if (muted) { btn.classList.add('forced-off'); btn.textContent = 'Mute [ON]'; }
    else { btn.classList.remove('forced-off'); btn.textContent = 'Mute'; }
}
function toggleBuzzerMute() {
    const btn = document.getElementById('buzzerMuteBtn');
    const currentlyMuted = btn.classList.contains('forced-off');
    fetch('/api/setBuzzerMute', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({muted: !currentlyMuted})
    }).then(r => r.json()).then(d => {
        updateBuzzerMuteBtn(d.muted);
    }).catch(e => { console.error('Buzzer mute error:', e); alert('Failed to toggle buzzer mute'); });
}
function toggleRelayForceOff() {
    const btn = document.getElementById('relayForceOffBtn');
    const currentlyForced = btn.classList.contains('forced-off');
    fetch('/api/setRelayForce', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({forceOff: !currentlyForced})
    }).then(r => r.json()).then(d => {
        updateRelayForceBtn(d.forceOff);
    }).catch(e => { console.error('Relay force error:', e); alert('Failed to toggle relay'); });
}
function applyAllPID(btn) {
    const kp = parseFloat(document.getElementById('kpInput').value);
    const ki = parseFloat(document.getElementById('kiInput').value);
    const kd = parseFloat(document.getElementById('kdInput').value);
    const target = parseFloat(document.getElementById('targetInput').value);
    const brewKp = parseFloat(document.getElementById('brewKpInput').value);
    const brewKi = parseFloat(document.getElementById('brewKiInput').value);
    const brewKd = parseFloat(document.getElementById('brewKdInput').value);
    const boostSeconds = parseInt(document.getElementById('brewBoostSecondsInput').value);
    const boostDuty = parseInt(document.getElementById('brewBoostDutyInput').value);
    const delaySeconds = parseInt(document.getElementById('brewDelaySecondsInput').value);
    const delayDuty = parseInt(document.getElementById('brewDelayDutyInput').value);
    if (isNaN(kp)||kp<0||kp>500) { alert('Kp: 0-500'); return; }
    if (isNaN(ki)||ki<0||ki>50) { alert('Ki: 0-50'); return; }
    if (isNaN(kd)||kd<0||kd>2000) { alert('Kd: 0-2000'); return; }
    if (isNaN(target)||target<0||target>120) { alert('Temp: 0-120\u00b0C'); return; }
    if (isNaN(brewKp)||brewKp<0||brewKp>500) { alert('Brew Kp: 0-500'); return; }
    if (isNaN(brewKi)||brewKi<0||brewKi>50) { alert('Brew Ki: 0-50'); return; }
    if (isNaN(brewKd)||brewKd<0||brewKd>2000) { alert('Brew Kd: 0-2000'); return; }
    if (isNaN(boostSeconds)||boostSeconds<0||boostSeconds>30) { alert('Boost: 0-30s'); return; }
    if (isNaN(boostDuty)||boostDuty<0||boostDuty>100) { alert('Boost duty: 0-100%'); return; }
    if (isNaN(delaySeconds)||delaySeconds<0||delaySeconds>30) { alert('Delay: 0-30s'); return; }
    if (isNaN(delayDuty)||delayDuty<0||delayDuty>100) { alert('Delay duty: 0-100%'); return; }
    fetch('/api/setAllSettings', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({kp, ki, kd, target, brewKp, brewKi, brewKd, boostSeconds, boostDuty, delaySeconds, delayDuty})
    }).then(r => r.json()).then(() => {
        const txt = btn.textContent;
        btn.textContent = 'Applied \u2713'; btn.style.backgroundColor = '#28a745';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Apply all error:', e); alert('Failed to apply PID values'); });
}
function resetAllPID(btn) {
    if (!confirm('Reset all PID values (heating + brewing) to factory defaults?')) return;
    fetch('/api/resetAllSettings', {
        method: 'POST', headers: {'Content-Type':'application/json'}
    }).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            document.getElementById('kpInput').value = d.kp;
            document.getElementById('kiInput').value = d.ki;
            document.getElementById('kdInput').value = d.kd;
            document.getElementById('targetInput').value = d.target;
            document.getElementById('brewKpInput').value = d.brewKp;
            document.getElementById('brewKiInput').value = d.brewKi;
            document.getElementById('brewKdInput').value = d.brewKd;
            document.getElementById('brewBoostSecondsInput').value = d.boostSeconds;
            document.getElementById('brewBoostDutyInput').value = d.boostDuty;
            document.getElementById('brewDelaySecondsInput').value = d.delaySeconds;
            document.getElementById('brewDelayDutyInput').value = d.delayDuty;
            const txt = btn.textContent;
            btn.textContent = 'Reset Complete \u2713';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('Reset all error:', e); alert('Failed to reset PID values'); });
}
function applyTargetTemp(btn) {
    const target = parseFloat(document.getElementById('targetInput').value);
    if (isNaN(target) || target < 0 || target > 120) { alert('Temp: 0\u2013120\u00b0C'); return; }
    fetch('/api/setTarget', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({ target }),
    }).then(r => r.json()).then(() => {
        const txt = btn.textContent;
        btn.textContent = 'Set \u2713'; btn.style.backgroundColor = '#103715';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Set temp error:', e); alert('Failed to set temperature'); });
}

function resetPIDMemory(btn) {
    fetch('/api/resetPIDMemory', {
        method: 'POST', headers: {'Content-Type':'application/json'}
    }).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            const txt = btn.textContent;
            btn.textContent = 'Memory Cleared \u2713';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('PID memory reset error:', e); alert('Failed to reset PID memory'); });
}

function applyBrewSettings(btn) {
    const boostSeconds = parseInt(document.getElementById('brewBoostSecondsInput').value);
    const boostDuty = parseInt(document.getElementById('brewBoostDutyInput').value);
    const delaySeconds = parseInt(document.getElementById('brewDelaySecondsInput').value);
    const delayDuty = parseInt(document.getElementById('brewDelayDutyInput').value);
    if (isNaN(boostSeconds)||boostSeconds<0||boostSeconds>30) { alert('Boost: 0-30s'); return; }
    if (isNaN(boostDuty)||boostDuty<0||boostDuty>100) { alert('Boost duty: 0-100%'); return; }
    if (isNaN(delaySeconds)||delaySeconds<0||delaySeconds>30) { alert('Delay: 0-30s'); return; }
    if (isNaN(delayDuty)||delayDuty<0||delayDuty>100) { alert('Delay duty: 0-100%'); return; }
    fetch('/api/setBrewTiming', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({boostSeconds, boostDuty, delaySeconds, delayDuty})
    }).then(r=>r.json()).then(() => {
        const txt = btn.textContent;
        btn.textContent = 'Applied \u2713'; btn.style.backgroundColor = '#28a745';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Brew update error:', e); alert('Failed to update brew timing'); });
}
function resetBrewSettings(btn) {
    if (!confirm('Reset brew timing to factory defaults?')) return;
    fetch('/api/resetBrewTiming', {
        method: 'POST', headers: {'Content-Type':'application/json'}
    }).then(r=>r.json()).then(d=>{
        if (d.status === 'ok') {
            document.getElementById('brewBoostSecondsInput').value = d.boostSeconds;
            document.getElementById('brewBoostDutyInput').value = d.boostDuty;
            document.getElementById('brewDelaySecondsInput').value = d.delaySeconds;
            document.getElementById('brewDelayDutyInput').value = d.delayDuty;
            const txt = btn.textContent;
            btn.textContent = 'Reset Complete \u2713';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('Brew reset error:', e); alert('Failed to reset brew timing'); });
}
function init() {
    console.log('Dashboard init');
    fetch('/api/getSettings').then(r=>r.json()).then(d=>{
        document.getElementById('kpInput').value = d.kp;
        document.getElementById('kiInput').value = d.ki;
        document.getElementById('kdInput').value = d.kd;
        document.getElementById('targetInput').value = d.target;
        document.getElementById('brewKpInput').value = d.brewKp;
        document.getElementById('brewKiInput').value = d.brewKi;
        document.getElementById('brewKdInput').value = d.brewKd;
        document.getElementById('brewBoostSecondsInput').value = d.boostSeconds;
        document.getElementById('brewBoostDutyInput').value = d.boostDuty;
        document.getElementById('brewDelaySecondsInput').value = d.delaySeconds;
        document.getElementById('brewDelayDutyInput').value = d.delayDuty;
        updateBuzzerMuteBtn(d.buzzerMute);
    }).catch(e => console.error('Failed to load settings:', e));
    fetchRealData(); setInterval(fetchRealData, 1000);
}
window.addEventListener('load', init);
window.dashboardAPI = {
    updateValues: (t,s,p,d) => { currentTemp=t; setTemp=s; pidOutput=p; dutyCycle=d; updateDashboard(); },
    setConnectionStatus: (c) => {
        const b = document.getElementById('statusBadge');
        b.textContent = c ? 'Connected' : 'Disconnected';
        c ? b.classList.add('connected') : b.classList.remove('connected');
    },
    setRelayError: (e) => {
        const bar = document.getElementById('dutyBar');
        e ? bar.classList.add('error') : bar.classList.remove('error');
    }
};
)rawliteral";

/**
 * @brief Send HTTP response with content type
 */
void sendResponse(WiFiClient &client, const char* content, const char* contentType) {
    client.println("HTTP/1.1 200 OK");
    client.print("Content-Type: ");
    client.println(contentType);
    client.println("Connection: close");
    client.println();
    
    size_t len = strlen_P(content);
    char buffer[WEB_SEND_BUFFER_SIZE];
    
    for (size_t i = 0; i < len; i += WEB_SEND_BUFFER_SIZE) {
        size_t chunk = min((size_t)WEB_SEND_BUFFER_SIZE, len - i);
        memcpy_P(buffer, content + i, chunk);
        client.write((uint8_t*)buffer, chunk);
    }
}

/**
 * @brief Initialize WiFi and Web Server
 * Tries to connect to home WiFi first, falls back to AP mode if it fails
 */
void setupWebServer() {
    Serial.printf("[WEB] Setting WiFi mode to STA, connecting to '%s'...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_ATTEMPTS) {
        delay(WIFI_CONNECT_DELAY_MS);
        attempts++;
        Serial.printf("[WEB] WiFi attempt %d/%d — status=%d\n", attempts, WIFI_CONNECT_ATTEMPTS, (int)WiFi.status());
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WEB] Connected to %s | http://%s\n", WIFI_SSID, WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[WEB] STA failed after %d attempts — switching to AP mode\n", attempts);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        delay(100);
        Serial.printf("[WEB] AP mode: %s | http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    }
    
    server.begin();
    Serial.printf("[WEB] HTTP server started on port %d\n", WEBSERVER_PORT);
}

/**
 * @brief Get the current IP address as a string
 * @return String containing the IP address
 */
String getIPAddress() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (WiFi.getMode() == WIFI_AP) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

/**
 * @brief Handle incoming web requests
 */
void handleWebServer() {
    // Heartbeat: confirm the loop is running (every 5 seconds)
    static unsigned long lastHeartbeat = 0;
    static unsigned long callCount = 0;
    callCount++;
    if (millis() - lastHeartbeat > 5000) {
        Serial.printf("[WEB] Alive — %lu calls/5s | heap=%u | WiFi=%d\n",
                      callCount, ESP.getFreeHeap(), (int)WiFi.status());
        callCount = 0;
        lastHeartbeat = millis();
    }

    WiFiClient client = server.available();
    
    if (client) {
        Serial.printf("[WEB] Client connected from %s\n", client.remoteIP().toString().c_str());
        String requestLine = "";
        String currentLine = "";
        bool isFirstLine = true;
        int contentLength = 0;
        
        unsigned long clientTimeout = millis();
        while (client.connected()) {
            if (millis() - clientTimeout > 3000) {
                Serial.println("[WEB] Client timeout - closing");
                break;
            }
            if (client.available()) {
                clientTimeout = millis(); // reset timeout on data
                char c = client.read();
                
                if (c == '\n') {
                    // Check for Content-Length header
                    if (currentLine.startsWith("Content-Length:")) {
                        contentLength = currentLine.substring(15).toInt();
                    }
                    
                    if (currentLine.length() == 0) {
                        // End of headers - now read body if Content-Length > 0
                        String body = "";
                        if (contentLength > 0) {
                            // Wait for body data with timeout
                            unsigned long bodyStart = millis();
                            while (body.length() < contentLength && (millis() - bodyStart) < WEB_BODY_READ_TIMEOUT_MS) {
                                if (client.available()) {
                                    body += (char)client.read();
                                } else {
                                    vTaskDelay(1 / portTICK_PERIOD_MS);
                                }
                            }
                        }
                        
                        // Send response based on requested path
                        if (requestLine.indexOf("GET / ") >= 0 || requestLine.indexOf("GET /index.html") >= 0) {
                            sendResponse(client, index_html, "text/html");
                        }
                        else if (requestLine.indexOf("GET /style.css") >= 0) {
                            sendResponse(client, style_css, "text/css");
                        }
                        else if (requestLine.indexOf("GET /script.js") >= 0) {
                            sendResponse(client, script_js, "application/javascript");
                        }
                        else if (requestLine.indexOf("GET /api/data") >= 0) {
                            // Read state with mutex protection
                            STATE_LOCK();
                            float currentTemp = state.currentTemp;
                            float setTemp = state.setTemp;
                            float pidOutput = state.pidOutput;
                            bool brewMode = state.brewMode;
                            STATE_UNLOCK();
                            
                            // Send real-time data as JSON
                            String json = "{";
                            json += "\"currentTemp\":" + String(currentTemp, 1) + ",";
                            json += "\"setTemp\":" + String(setTemp, 1) + ",";
                            json += "\"pidOutput\":" + String(pidOutput, 0) + ",";
                            json += "\"dutyCycle\":" + String((pidOutput / SSR_WINDOW_MS * 100.0), 1) + ",";
                            json += "\"error\":" + String(isEmergencyStopActive() ? "true" : "false") + ",";
                            json += "\"brewMode\":" + String(brewMode ? "true" : "false") + ",";
                            json += "\"brewBoostPhase\":" + String(isBrewBoostPhase() ? "true" : "false") + ",";
                            json += "\"brewDelayPhase\":" + String(isBrewDelayPhase() ? "true" : "false") + ",";
                            json += "\"relayForceOff\":" + String(isRelayForceOff() ? "true" : "false");
                            json += "}";
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                        }
                        else if (requestLine.indexOf("GET /api/getSettings") >= 0) {
                            // Return all settings in one call
                            double kp, ki, kd;
                            getPIDTunings(kp, ki, kd);
                            double bkp, bki, bkd;
                            int bboost, bdelay, bboostDuty, bdelayDuty;
                            getBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            STATE_LOCK();
                            float setTemp = state.setTemp;
                            STATE_UNLOCK();
                            String json = "{";
                            json += "\"kp\":" + String(kp, 1) + ",";
                            json += "\"ki\":" + String(ki, 3) + ",";
                            json += "\"kd\":" + String(kd, 1) + ",";
                            json += "\"target\":" + String(setTemp, 1) + ",";
                            json += "\"brewKp\":" + String(bkp, 1) + ",";
                            json += "\"brewKi\":" + String(bki, 3) + ",";
                            json += "\"brewKd\":" + String(bkd, 1) + ",";
                            json += "\"boostSeconds\":" + String(bboost) + ",";
                            json += "\"boostDuty\":" + String(bboostDuty) + ",";
                            json += "\"delaySeconds\":" + String(bdelay) + ",";
                            json += "\"delayDuty\":" + String(bdelayDuty) + ",";
                            json += "\"buzzerMute\":" + String(getBuzzerMute() ? "true" : "false");
                            json += "}";
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                        }
                        else if (requestLine.indexOf("POST /api/setBuzzerMute") >= 0) {
                            bool muted = body.indexOf("\"muted\":true") >= 0;
                            setBuzzerMute(muted);
                            savePIDToStorage();
                            String json = "{\"status\":\"ok\",\"muted\":";
                            json += muted ? "true" : "false";
                            json += "}";
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.printf("[WEB] Buzzer mute: %s\n", muted ? "ON" : "OFF");
                        }
                        else if (requestLine.indexOf("POST /api/setTarget") >= 0) {
                            double target = 0;
                            int targetIdx = body.indexOf("\"target\":");
                            if (targetIdx >= 0) target = body.substring(targetIdx + 9).toDouble();
                            setTargetTemp(target);
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println("{\"status\":\"ok\"}");
                            Serial.printf("[WEB] Target temp set: %.1f\n", target);
                        }
                        else if (requestLine.indexOf("POST /api/setAllSettings") >= 0) {
                            double kp = 0, ki = 0, kd = 0, target = 0;
                            double bkp = 0, bki = 0, bkd = 0;
                            int bboost = DEFAULT_BREW_BOOST_SECONDS;
                            int bdelay = DEFAULT_BREW_DELAY_SECONDS;
                            int bboostDuty = DEFAULT_BREW_BOOST_DUTY_CYCLE;
                            int bdelayDuty = DEFAULT_BREW_DELAY_DUTY_CYCLE;
                            int kpIdx = body.indexOf("\"kp\":");
                            int kiIdx = body.indexOf("\"ki\":");
                            int kdIdx = body.indexOf("\"kd\":");
                            int targetIdx = body.indexOf("\"target\":");
                            int brewKpIdx = body.indexOf("\"brewKp\":");
                            int brewKiIdx = body.indexOf("\"brewKi\":");
                            int brewKdIdx = body.indexOf("\"brewKd\":");
                            int boostIdx = body.indexOf("\"boostSeconds\":");
                            int boostDutyIdx = body.indexOf("\"boostDuty\":");
                            int delayIdx = body.indexOf("\"delaySeconds\":");
                            int delayDutyIdx = body.indexOf("\"delayDuty\":");
                            if (kpIdx >= 0) kp = body.substring(kpIdx + 5).toDouble();
                            if (kiIdx >= 0) ki = body.substring(kiIdx + 5).toDouble();
                            if (kdIdx >= 0) kd = body.substring(kdIdx + 5).toDouble();
                            if (targetIdx >= 0) target = body.substring(targetIdx + 9).toDouble();
                            if (brewKpIdx >= 0) bkp = body.substring(brewKpIdx + 9).toDouble();
                            if (brewKiIdx >= 0) bki = body.substring(brewKiIdx + 9).toDouble();
                            if (brewKdIdx >= 0) bkd = body.substring(brewKdIdx + 9).toDouble();
                            if (boostIdx >= 0) bboost = body.substring(boostIdx + 15).toInt();
                            if (boostDutyIdx >= 0) bboostDuty = body.substring(boostDutyIdx + 12).toInt();
                            if (delayIdx >= 0) bdelay = body.substring(delayIdx + 15).toInt();
                            if (delayDutyIdx >= 0) bdelayDuty = body.substring(delayDutyIdx + 12).toInt();
                            setPIDTunings(kp, ki, kd);
                            setTargetTemp(target);
                            setBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println("{\"status\":\"ok\"}");
                            Serial.printf("[WEB] All settings: Kp=%.1f Ki=%.3f Kd=%.1f T=%.1f | BKp=%.1f BKi=%.3f BKd=%.1f Boost=%ds@%d%% Delay=%ds@%d%%\n",
                                         kp, ki, kd, target, bkp, bki, bkd, bboost, bboostDuty, bdelay, bdelayDuty);
                        }
                        else if (requestLine.indexOf("POST /api/resetAllSettings") >= 0) {
                            resetPIDToDefaults();
                            resetBrewPIDToDefaults();
                            double kp, ki, kd;
                            getPIDTunings(kp, ki, kd);
                            STATE_LOCK();
                            float setTemp = state.setTemp;
                            STATE_UNLOCK();
                            double bkp, bki, bkd;
                            int bboost, bdelay, bboostDuty, bdelayDuty;
                            getBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            String json = "{";
                            json += "\"status\":\"ok\",";
                            json += "\"kp\":" + String(kp, 1) + ",";
                            json += "\"ki\":" + String(ki, 3) + ",";
                            json += "\"kd\":" + String(kd, 1) + ",";
                            json += "\"target\":" + String(setTemp, 1) + ",";
                            json += "\"brewKp\":" + String(bkp, 1) + ",";
                            json += "\"brewKi\":" + String(bki, 3) + ",";
                            json += "\"brewKd\":" + String(bkd, 1) + ",";
                            json += "\"boostSeconds\":" + String(bboost) + ",";
                            json += "\"boostDuty\":" + String(bboostDuty) + ",";
                            json += "\"delaySeconds\":" + String(bdelay) + ",";
                            json += "\"delayDuty\":" + String(bdelayDuty);
                            json += "}";
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.println("[WEB] All settings reset to defaults");
                        }
                        else if (requestLine.indexOf("POST /api/resetPIDMemory") >= 0) {
                            resetPIDMemory();
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println("{\"status\":\"ok\"}");
                            Serial.println("[WEB] PID memory reset (integral zeroed)");
                        }
                        else if (requestLine.indexOf("POST /api/setBrewTiming") >= 0) {
                            // Preserve existing brew PID tunings, update only timing
                            double bkp, bki, bkd;
                            int bboost, bdelay, bboostDuty, bdelayDuty;
                            getBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            int boostIdx = body.indexOf("\"boostSeconds\":");
                            int boostDutyIdx = body.indexOf("\"boostDuty\":");
                            int delayIdx = body.indexOf("\"delaySeconds\":");
                            int delayDutyIdx = body.indexOf("\"delayDuty\":");
                            if (boostIdx >= 0) bboost = body.substring(boostIdx + 15).toInt();
                            if (boostDutyIdx >= 0) bboostDuty = body.substring(boostDutyIdx + 12).toInt();
                            if (delayIdx >= 0) bdelay = body.substring(delayIdx + 15).toInt();
                            if (delayDutyIdx >= 0) bdelayDuty = body.substring(delayDutyIdx + 12).toInt();
                            setBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println("{\"status\":\"ok\"}");
                            Serial.printf("[WEB] Brew timing updated: Boost=%ds@%d%%, Delay=%ds@%d%%\n",
                                         bboost, bboostDuty, bdelay, bdelayDuty);
                        }
                        else if (requestLine.indexOf("POST /api/resetBrewTiming") >= 0) {
                            // Preserve existing brew PID tunings, reset only timing
                            double bkp, bki, bkd;
                            int bboost, bdelay, bboostDuty, bdelayDuty;
                            getBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            setBrewPIDTunings(bkp, bki, bkd,
                                             DEFAULT_BREW_BOOST_SECONDS, DEFAULT_BREW_DELAY_SECONDS,
                                             DEFAULT_BREW_BOOST_DUTY_CYCLE, DEFAULT_BREW_DELAY_DUTY_CYCLE);
                            getBrewPIDTunings(bkp, bki, bkd, bboost, bdelay, bboostDuty, bdelayDuty);
                            String json = "{";
                            json += "\"status\":\"ok\",";
                            json += "\"boostSeconds\":" + String(bboost) + ",";
                            json += "\"boostDuty\":" + String(bboostDuty) + ",";
                            json += "\"delaySeconds\":" + String(bdelay) + ",";
                            json += "\"delayDuty\":" + String(bdelayDuty);
                            json += "}";
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.println("[WEB] Brew timing reset to defaults");
                        }
                        else if (requestLine.indexOf("POST /api/setRelayForce") >= 0) {
                            bool forceOff = body.indexOf("\"forceOff\":true") >= 0;
                            setRelayForceOff(forceOff);
                            String json = "{\"status\":\"ok\",\"forceOff\":";
                            json += forceOff ? "true" : "false";
                            json += "}";
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.printf("[WEB] Relay force-off: %s\n", forceOff ? "true" : "false");
                        }
                        else {
                            Serial.println("[WEB] 404: " + requestLine);
                            client.println("HTTP/1.1 404 Not Found");
                            client.println("Connection: close");
                            client.println();
                        }
                        break;
                    } else {
                        if (isFirstLine) {
                            requestLine = currentLine;
                            isFirstLine = false;
                        }
                        currentLine = "";
                    }
                } else if (c != '\r') {
                    currentLine += c;
                }
            } else {
                // No data yet — yield so the lwIP/WiFi tasks on Core 0 can run
                vTaskDelay(1 / portTICK_PERIOD_MS);
            }
        }
        
        // Flush TCP send buffer so all response bytes leave before we close
        client.flush();
        vTaskDelay(10 / portTICK_PERIOD_MS);
        client.stop();
        Serial.printf("[WEB] Client done | heap=%u | stack_hwm=%u\n",
                      ESP.getFreeHeap(), uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    }
}
