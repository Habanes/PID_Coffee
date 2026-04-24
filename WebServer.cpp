/**
 * @file WebServer.cpp
 * @brief WiFi web server serving static dashboard files
 */

#include "WebServer.h"
#include "State.h"
#include "Controls.h"
#include <WiFi.h>

// WiFi Credentials - Change these to your home WiFi
const char* wifi_ssid = "BabaLan";      // Replace with your WiFi name
const char* wifi_password = "bittegibmirinternet"; // Replace with your WiFi password

// Fallback AP mode credentials (if WiFi connection fails)
const char* ap_ssid = "QuickMill-PID";
const char* ap_password = "espresso123";

// Web Server on port 80
WiFiServer server(80);

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
            <h1>QuickMill PID Controller</h1>
            <div class="status-badge" id="statusBadge">Disconnected</div>
        </header>

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

            <div class="card">
                <div class="card-title">Duty Cycle</div>
                <div class="value-display" id="dutyCycle">
                    <span class="value">45.0</span>
                    <span class="unit">%</span>
                </div>
            </div>
        </div>

        <div class="chart-container">
            <div class="card">
                <div class="card-title">Temperature History</div>
                <canvas id="tempChart"></canvas>
            </div>
        </div>

        <div class="card">
            <div class="card-title">Relay Duty Cycle</div>
            <div class="duty-bar-container">
                <div class="duty-bar" id="dutyBar" style="width: 45%;">
                    <span class="duty-bar-text">45%</span>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-title">Testing Controls</div>
            <div style="display: flex; align-items: center; gap: 15px; flex-wrap: wrap;">
                <button class="btn-relay-override" id="relayForceOffBtn" onclick="toggleRelayForceOff()">Relay: ON (PID controlled)</button>
                <span style="color:#666; font-size:0.9em;">Force relay OFF to let cold water cool the system without heating.</span>
            </div>
        </div>

        <div class="card">
            <div class="card-title">PID Configuration</div>
            <div class="pid-controls">
                <div class="control-grid">
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
                        <input type="number" id="kdInput" step="0.1" value="0.0" min="0" max="100">
                    </div>
                    <div class="control-group">
                        <label for="targetInput">Target Temperature (°C)</label>
                        <input type="number" id="targetInput" step="0.5" value="93.0" min="0" max="120">
                    </div>
                </div>
                <div style="display: flex; gap: 10px; flex-wrap: wrap;">
                    <button class="btn-primary" onclick="applyPIDSettings(this)" style="flex: 1;">Apply Settings</button>
                    <button class="btn-reset" onclick="resetPIDSettings(this)" style="flex: 1;">Reset to Defaults</button>
                    <button class="btn-reset" onclick="resetPIDMemory(this)" style="flex: 1; background: linear-gradient(135deg, #6c757d 0%, #495057 100%);">Reset PID Memory</button>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-title">Brew Mode Configuration</div>
            <div class="brew-status-row">
                <span class="brew-status-indicator" id="brewStatusBadge">INACTIVE</span>
                <span class="brew-status-label" id="brewStatusLabel">Brew mode off &mdash; press GPIO&nbsp;0 button to activate</span>
            </div>
            <div class="pid-controls">
                <div class="control-grid">
                    <div class="control-group">
                        <label for="brewKpInput">Brew Kp (Proportional)</label>
                        <input type="number" id="brewKpInput" step="0.1" value="50.0" min="0" max="500">
                    </div>
                    <div class="control-group">
                        <label for="brewKiInput">Brew Ki (Integral)</label>
                        <input type="number" id="brewKiInput" step="0.1" value="0.5" min="0" max="50">
                    </div>
                    <div class="control-group">
                        <label for="brewKdInput">Brew Kd (Derivative)</label>
                        <input type="number" id="brewKdInput" step="0.1" value="8.0" min="0" max="100">
                    </div>
                    <div class="control-group">
                        <label for="brewBoostInput">Pre-Boost Duration (s)</label>
                        <input type="number" id="brewBoostInput" step="1" value="5" min="1" max="30">
                    </div>
                </div>
                <div style="display: flex; gap: 10px;">
                    <button class="btn-brew" onclick="applyBrewSettings(this)" style="flex: 1;">Apply Brew Settings</button>
                    <button class="btn-reset" onclick="resetBrewSettings(this)" style="flex: 1;">Reset Brew Defaults</button>
                </div>
            </div>
        </div>

        <footer>
            <p>QuickMill Orione 3000 | ESP32-S3 PID Temperature Controller</p>
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
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    padding: 20px;
    color: #333;
}
.container { max-width: 1200px; margin: 0 auto; }
.container > .card { margin-bottom: 20px; }
header {
    background: rgba(255, 255, 255, 0.95);
    padding: 20px 30px;
    border-radius: 15px;
    margin-bottom: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
}
header h1 { font-size: 2em; color: #667eea; }
.status-badge {
    padding: 8px 20px;
    border-radius: 20px;
    font-weight: bold;
    font-size: 0.9em;
    background: #ff6b6b;
    color: white;
    animation: pulse 2s infinite;
}
.status-badge.connected { background: #51cf66; animation: none; }
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
.grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 20px;
    margin-bottom: 20px;
}
.card {
    background: rgba(255, 255, 255, 0.95);
    padding: 25px;
    border-radius: 15px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
    transition: transform 0.3s ease, box-shadow 0.3s ease;
}
.card:hover {
    transform: translateY(-5px);
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.15);
}
.card-title {
    font-size: 1em;
    color: #666;
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
    color: #667eea;
    margin-right: 10px;
}
.value-display .unit { font-size: 1.5em; color: #999; }
.chart-container { margin-bottom: 20px; }
.chart-container canvas { max-height: 300px; }
.duty-bar-container {
    width: 100%;
    height: 40px;
    background: #e9ecef;
    border-radius: 10px;
    overflow: hidden;
    position: relative;
}
.duty-bar {
    height: 100%;
    background: #51cf66;
    transition: width 0.5s ease, background-color 0.3s ease;
    display: flex;
    align-items: center;
    justify-content: center;
    min-width: 60px;
}
.duty-bar.error {
    background: #ff6b6b;
}
.duty-bar-text {
    color: white;
    font-weight: bold;
    font-size: 1.1em;
    text-shadow: 1px 1px 2px rgba(0, 0, 0, 0.3);
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
    color: #666;
    font-weight: 600;
}
.control-group input {
    padding: 12px 15px;
    border: 2px solid #e9ecef;
    border-radius: 8px;
    font-size: 1.1em;
    font-weight: bold;
    color: #667eea;
    background: white;
    transition: border-color 0.3s ease, box-shadow 0.3s ease;
}
.control-group input:focus {
    outline: none;
    border-color: #667eea;
    box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
}
.control-group input:hover {
    border-color: #b8c5f0;
}
.btn-primary {
    padding: 15px 30px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    border: none;
    border-radius: 10px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.3s ease;
    box-shadow: 0 4px 15px rgba(102, 126, 234, 0.3);
}
.btn-primary:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(102, 126, 234, 0.4);
}
.btn-primary:active {
    transform: translateY(0);
    box-shadow: 0 2px 10px rgba(102, 126, 234, 0.3);
}
.btn-reset {
    padding: 15px 30px;
    background: linear-gradient(135deg, #ff6b6b 0%, #ee5a6f 100%);
    color: white;
    border: none;
    border-radius: 10px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.3s ease;
    box-shadow: 0 4px 15px rgba(255, 107, 107, 0.3);
}
.btn-reset:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(255, 107, 107, 0.4);
}
.btn-reset:active {
    transform: translateY(0);
    box-shadow: 0 2px 10px rgba(255, 107, 107, 0.3);
}
.btn-brew {
    padding: 15px 30px;
    background: linear-gradient(135deg, #fd7c20 0%, #e06010 100%);
    color: white;
    border: none;
    border-radius: 10px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.3s ease;
    box-shadow: 0 4px 15px rgba(253, 124, 32, 0.3);
}
.btn-brew:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(253, 124, 32, 0.4);
}
.btn-brew:active {
    transform: translateY(0);
    box-shadow: 0 2px 10px rgba(253, 124, 32, 0.3);
}
.btn-relay-override {
    padding: 15px 30px;
    background: linear-gradient(135deg, #28a745 0%, #1e7e34 100%);
    color: white;
    border: none;
    border-radius: 10px;
    font-size: 1.1em;
    font-weight: bold;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.3s ease, background 0.3s ease;
    box-shadow: 0 4px 15px rgba(40, 167, 69, 0.3);
}
.btn-relay-override:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(40, 167, 69, 0.4);
}
.btn-relay-override.forced-off {
    background: linear-gradient(135deg, #dc3545 0%, #b02030 100%);
    box-shadow: 0 4px 15px rgba(220, 53, 69, 0.4);
}
.btn-relay-override.forced-off:hover {
    box-shadow: 0 6px 20px rgba(220, 53, 69, 0.5);
}
.brew-status-row {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 20px;
}
.brew-status-indicator {
    padding: 6px 16px;
    border-radius: 20px;
    font-weight: bold;
    font-size: 0.85em;
    background: #e9ecef;
    color: #888;
    transition: all 0.3s ease;
    white-space: nowrap;
}
.brew-status-indicator.active {
    background: #fd7c20;
    color: white;
    animation: pulse 1s infinite;
}
.brew-status-indicator.boost {
    background: #ff4444;
    color: white;
    animation: pulse 0.5s infinite;
}
.brew-status-label {
    color: #888;
    font-size: 0.9em;
}
footer {
    background: rgba(255, 255, 255, 0.95);
    padding: 15px;
    border-radius: 15px;
    text-align: center;
    margin-top: 20px;
    color: #666;
}
@media (max-width: 768px) {
    header { flex-direction: column; text-align: center; gap: 15px; }
    header h1 { font-size: 1.5em; }
    .value-display .value { font-size: 2.5em; }
    .grid { grid-template-columns: 1fr; }
}
)rawliteral";

const char script_js[] PROGMEM = R"rawliteral(
let currentTemp = 92.5, setTemp = 93.0, pidOutput = 450, dutyCycle = 45.0;
if (typeof Chart === 'undefined') { console.error('Chart.js not loaded!'); }
const ctx = document.getElementById('tempChart').getContext('2d');
const chart = new Chart(ctx, {
    type: 'line',
    data: { labels: [], datasets: [{
        label: 'Current Temp (°C)', data: [], borderColor: '#667eea',
        backgroundColor: 'rgba(102, 126, 234, 0.1)', borderWidth: 3, tension: 0.4, fill: true, pointRadius: 0
    }, {
        label: 'Set Temp (°C)', data: [], borderColor: '#ff6b6b',
        backgroundColor: 'transparent', borderWidth: 2, borderDash: [5, 5], tension: 0, fill: false, pointRadius: 0
    }]},
    options: {
        responsive: true, maintainAspectRatio: true,
        plugins: { legend: { display: true, position: 'top' }, tooltip: { mode: 'index', intersect: false } },
        scales: {
            x: { display: true, grid: { color: 'rgba(0,0,0,0.05)' }, ticks: { maxTicksLimit: 10 } },
            y: { display: true, grid: { color: 'rgba(0,0,0,0.05)' }, ticks: { callback: v => v+'°C' } }
        },
        interaction: { mode: 'nearest', axis: 'x', intersect: false }
    }
});
const MAX_DATA_POINTS = 200;
function updateDashboard() {
    document.querySelector('#currentTemp .value').textContent = currentTemp.toFixed(1);
    document.querySelector('#setTemp .value').textContent = setTemp.toFixed(1);
    document.querySelector('#pidOutput .value').textContent = Math.round(pidOutput);
    document.querySelector('#dutyCycle .value').textContent = dutyCycle.toFixed(1);
    const dutyBar = document.getElementById('dutyBar');
    dutyBar.style.width = dutyCycle + '%';
    dutyBar.querySelector('.duty-bar-text').textContent = dutyCycle.toFixed(1) + '%';
    const timestamp = new Date().toLocaleTimeString();
    chart.data.labels.push(timestamp);
    chart.data.datasets[0].data.push(currentTemp);
    chart.data.datasets[1].data.push(setTemp);
    if (chart.data.labels.length > MAX_DATA_POINTS) {
        chart.data.labels.shift(); chart.data.datasets[0].data.shift(); chart.data.datasets[1].data.shift();
    }
    if (chart.data.datasets[0].data.length > 0) {
        const allTemps = [...chart.data.datasets[0].data, ...chart.data.datasets[1].data];
        const minTemp = Math.min(...allTemps);
        const maxTemp = Math.max(...allTemps);
        const range = maxTemp - minTemp;
        const margin = range * 0.1;
        chart.options.scales.y.min = Math.floor(minTemp - margin);
        chart.options.scales.y.max = Math.ceil(maxTemp + margin);
    }
    chart.update('none');
}
function fetchRealData() {
    fetch('/api/data').then(r => r.json()).then(data => {
        currentTemp = data.currentTemp; setTemp = data.setTemp;
        pidOutput = data.pidOutput; dutyCycle = data.dutyCycle;
        dashboardAPI.setRelayError(data.error);
        updateBrewStatus(data.brewMode, data.brewBoostPhase);
        updateRelayForceBtn(data.relayForceOff);
        updateDashboard();
    }).catch(e => console.error('Fetch error:', e));
}
function updateBrewStatus(brewMode, boostPhase) {
    const badge = document.getElementById('brewStatusBadge');
    const label = document.getElementById('brewStatusLabel');
    if (!badge || !label) return;
    if (brewMode && boostPhase) {
        badge.textContent = 'BOOST'; badge.className = 'brew-status-indicator boost';
        label.textContent = 'Pre-heating boost active \u2014 100% heater power';
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
        btn.textContent = 'Relay: FORCED OFF (testing)';
    } else {
        btn.classList.remove('forced-off');
        btn.textContent = 'Relay: ON (PID controlled)';
    }
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
function applyPIDSettings(btn) {
    const kp = parseFloat(document.getElementById('kpInput').value);
    const ki = parseFloat(document.getElementById('kiInput').value);
    const kd = parseFloat(document.getElementById('kdInput').value);
    const target = parseFloat(document.getElementById('targetInput').value);
    if (isNaN(kp)||isNaN(ki)||isNaN(kd)||isNaN(target)) { alert('Invalid input'); return; }
    if (kp<0||kp>500) { alert('Kp: 0-500'); return; }
    if (ki<0||ki>50) { alert('Ki: 0-50'); return; }
    if (kd<0||kd>100) { alert('Kd: 0-100'); return; }
    if (target<0||target>120) { alert('Temp: 0-120\u00b0C'); return; }
    fetch('/api/setPID', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({kp,ki,kd,target})
    }).then(r=>r.json()).then(d=>{
        const txt = btn.textContent;
        btn.textContent = 'Applied \u2713'; btn.style.backgroundColor = '#28a745';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Update error:', e); alert('Failed to update PID'); });
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
function resetPIDSettings(btn) {
    if (!confirm('Reset all PID settings to factory defaults?')) return;
    fetch('/api/resetPID', {
        method: 'POST', headers: {'Content-Type':'application/json'}
    }).then(r=>r.json()).then(d=>{
        if (d.status === 'ok') {
            document.getElementById('kpInput').value = d.kp;
            document.getElementById('kiInput').value = d.ki;
            document.getElementById('kdInput').value = d.kd;
            document.getElementById('targetInput').value = d.target;
            const txt = btn.textContent;
            btn.textContent = 'Reset Complete \u2713';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('Reset error:', e); alert('Failed to reset PID'); });
}
function applyBrewSettings(btn) {
    const kp = parseFloat(document.getElementById('brewKpInput').value);
    const ki = parseFloat(document.getElementById('brewKiInput').value);
    const kd = parseFloat(document.getElementById('brewKdInput').value);
    const boost = parseInt(document.getElementById('brewBoostInput').value);
    if (isNaN(kp)||isNaN(ki)||isNaN(kd)||isNaN(boost)) { alert('Invalid input'); return; }
    if (kp<0||kp>500) { alert('Brew Kp: 0-500'); return; }
    if (ki<0||ki>50) { alert('Brew Ki: 0-50'); return; }
    if (kd<0||kd>100) { alert('Brew Kd: 0-100'); return; }
    if (boost<1||boost>30) { alert('Boost: 1-30s'); return; }
    fetch('/api/setBrewSettings', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({kp,ki,kd,boost})
    }).then(r=>r.json()).then(d=>{
        const txt = btn.textContent;
        btn.textContent = 'Applied \u2713'; btn.style.backgroundColor = '#28a745';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Brew update error:', e); alert('Failed to update brew settings'); });
}
function resetBrewSettings(btn) {
    if (!confirm('Reset brew settings to factory defaults?')) return;
    fetch('/api/resetBrewSettings', {
        method: 'POST', headers: {'Content-Type':'application/json'}
    }).then(r=>r.json()).then(d=>{
        if (d.status === 'ok') {
            document.getElementById('brewKpInput').value = d.kp;
            document.getElementById('brewKiInput').value = d.ki;
            document.getElementById('brewKdInput').value = d.kd;
            document.getElementById('brewBoostInput').value = d.boost;
            const txt = btn.textContent;
            btn.textContent = 'Reset Complete \u2713';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('Brew reset error:', e); alert('Failed to reset brew settings'); });
}
function init() {
    console.log('Dashboard init');
    
    // Load current heating PID settings from ESP32
    fetch('/api/getPID').then(r=>r.json()).then(d=>{
        document.getElementById('kpInput').value = d.kp;
        document.getElementById('kiInput').value = d.ki;
        document.getElementById('kdInput').value = d.kd;
        document.getElementById('targetInput').value = d.target;
    }).catch(e => console.error('Failed to load PID settings:', e));

    // Load current brew settings from ESP32
    fetch('/api/getBrewSettings').then(r=>r.json()).then(d=>{
        document.getElementById('brewKpInput').value = d.kp;
        document.getElementById('brewKiInput').value = d.ki;
        document.getElementById('brewKdInput').value = d.kd;
        document.getElementById('brewBoostInput').value = d.boost;
    }).catch(e => console.error('Failed to load brew settings:', e));
    
    fetchRealData(); setInterval(fetchRealData, 1000);
    setTimeout(() => {
        const badge = document.getElementById('statusBadge');
        badge.textContent = 'Connected'; badge.classList.add('connected');
    }, 2000);
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
    
    // Send PROGMEM content directly
    size_t len = strlen_P(content);
    const size_t bufferSize = 1024;
    char buffer[bufferSize];
    
    for (size_t i = 0; i < len; i += bufferSize) {
        size_t chunk = min(bufferSize, len - i);
        memcpy_P(buffer, content + i, chunk);
        client.write((uint8_t*)buffer, chunk);
    }
}

/**
 * @brief Initialize WiFi and Web Server
 * Tries to connect to home WiFi first, falls back to AP mode if it fails
 */
void setupWebServer() {
    Serial.println("[WEB] Starting WiFi...");
    
    // Try to connect to existing WiFi network
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    
    Serial.print("[WEB] Connecting to WiFi: ");
    Serial.println(wifi_ssid);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // Successfully connected to WiFi
        Serial.println("\n[WEB] ========================================");
        Serial.println("[WEB] WiFi Connected!");
        Serial.printf("[WEB] SSID: %s\n", wifi_ssid);
        Serial.printf("[WEB] IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.println("[WEB] ========================================");
        Serial.printf("[WEB] Navigate to: http://%s\n", WiFi.localIP().toString().c_str());
        Serial.println("[WEB] ========================================");
    } else {
        // Failed to connect, start AP mode
        Serial.println("\n[WEB] WiFi connection failed. Starting Access Point...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(ap_ssid, ap_password);
        delay(100);
        
        IPAddress IP = WiFi.softAPIP();
        Serial.println("[WEB] ========================================");
        Serial.printf("[WEB] AP Started: %s\n", ap_ssid);
        Serial.printf("[WEB] Password: %s\n", ap_password);
        Serial.printf("[WEB] IP Address: %s\n", IP.toString().c_str());
        Serial.println("[WEB] ========================================");
        Serial.printf("[WEB] Navigate to: http://%s\n", IP.toString().c_str());
        Serial.println("[WEB] ========================================");
    }
    
    server.begin();
    Serial.println("[WEB] HTTP server started on port 80!");
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
    WiFiClient client = server.available();
    
    if (client) {
        Serial.println("[WEB] New client connected");
        
        String requestLine = "";
        String currentLine = "";
        bool isFirstLine = true;
        int contentLength = 0;
        
        while (client.connected()) {
            if (client.available()) {
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
                            while (body.length() < contentLength && (millis() - bodyStart) < 1000) {
                                if (client.available()) {
                                    body += (char)client.read();
                                } else {
                                    delay(1);  // Small delay to wait for data
                                }
                            }
                        }
                        
                        // Send response based on requested path
                        if (requestLine.indexOf("GET / ") >= 0 || requestLine.indexOf("GET /index.html") >= 0) {
                            Serial.println("[WEB] Serving index.html");
                            sendResponse(client, index_html, "text/html");
                        }
                        else if (requestLine.indexOf("GET /style.css") >= 0) {
                            Serial.println("[WEB] Serving style.css");
                            sendResponse(client, style_css, "text/css");
                        }
                        else if (requestLine.indexOf("GET /script.js") >= 0) {
                            Serial.println("[WEB] Serving script.js");
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
                            json += "\"dutyCycle\":" + String((pidOutput / 10.0), 1) + ",";
                            json += "\"error\":" + String(isEmergencyStopActive() ? "true" : "false") + ",";
                            json += "\"brewMode\":" + String(brewMode ? "true" : "false") + ",";
                            json += "\"brewBoostPhase\":" + String(isBrewBoostPhase() ? "true" : "false") + ",";
                            json += "\"relayForceOff\":" + String(isRelayForceOff() ? "true" : "false");
                            json += "}";
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                        }
                        else if (requestLine.indexOf("GET /api/getPID") >= 0) {
                            // Send current PID settings
                            double kp, ki, kd;
                            getPIDTunings(kp, ki, kd);
                            
                            STATE_LOCK();
                            float setTemp = state.setTemp;
                            STATE_UNLOCK();
                            
                            String json = "{";
                            json += "\"kp\":" + String(kp, 1) + ",";
                            json += "\"ki\":" + String(ki, 2) + ",";
                            json += "\"kd\":" + String(kd, 1) + ",";
                            json += "\"target\":" + String(setTemp, 1);
                            json += "}";
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.println("[WEB] Served /api/getPID");
                        }
                        else if (requestLine.indexOf("POST /api/setPID") >= 0) {
                            // Body was already read above after headers
                            // Parse JSON (simple parsing for our known format)
                            // Expected: {"kp":75.0,"ki":1.5,"kd":0.0,"target":93.0}
                            double kp = 0, ki = 0, kd = 0, target = 0;
                            int kpIdx = body.indexOf("\"kp\":");
                            int kiIdx = body.indexOf("\"ki\":");
                            int kdIdx = body.indexOf("\"kd\":");
                            int targetIdx = body.indexOf("\"target\":");
                            
                            if (kpIdx >= 0) kp = body.substring(kpIdx + 5).toDouble();
                            if (kiIdx >= 0) ki = body.substring(kiIdx + 5).toDouble();
                            if (kdIdx >= 0) kd = body.substring(kdIdx + 5).toDouble();
                            if (targetIdx >= 0) target = body.substring(targetIdx + 9).toDouble();
                            
                            Serial.printf("[WEB] Received body: %s\n", body.c_str());
                            
                            // Apply settings
                            setPIDTunings(kp, ki, kd);
                            setTargetTemp(target);
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println("{\"status\":\"ok\"}");
                            Serial.printf("[WEB] PID updated: Kp=%.1f, Ki=%.2f, Kd=%.1f, Target=%.1f\n", kp, ki, kd, target);
                        }
                        else if (requestLine.indexOf("POST /api/resetPID") >= 0) {
                            // Reset PID to factory defaults
                            resetPIDToDefaults();
                            
                            // Get current values to send back
                            double kp, ki, kd;
                            getPIDTunings(kp, ki, kd);
                            
                            STATE_LOCK();
                            float setTemp = state.setTemp;
                            STATE_UNLOCK();
                            
                            String json = "{";
                            json += "\"status\":\"ok\",";
                            json += "\"kp\":" + String(kp, 1) + ",";
                            json += "\"ki\":" + String(ki, 2) + ",";
                            json += "\"kd\":" + String(kd, 1) + ",";
                            json += "\"target\":" + String(setTemp, 1);
                            json += "}";
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.println("[WEB] PID reset to defaults");
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
                        else if (requestLine.indexOf("GET /api/getBrewSettings") >= 0) {
                            double bkp, bki, bkd;
                            int bboost;
                            getBrewPIDTunings(bkp, bki, bkd, bboost);
                            
                            String json = "{";
                            json += "\"kp\":" + String(bkp, 1) + ",";
                            json += "\"ki\":" + String(bki, 2) + ",";
                            json += "\"kd\":" + String(bkd, 1) + ",";
                            json += "\"boost\":" + String(bboost);
                            json += "}";
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.println("[WEB] Served /api/getBrewSettings");
                        }
                        else if (requestLine.indexOf("POST /api/setBrewSettings") >= 0) {
                            double bkp = 0, bki = 0, bkd = 0;
                            int bboost = DEFAULT_BREW_BOOST_SECONDS;
                            int kpIdx = body.indexOf("\"kp\":");
                            int kiIdx = body.indexOf("\"ki\":");
                            int kdIdx = body.indexOf("\"kd\":");
                            int boostIdx = body.indexOf("\"boost\":");
                            
                            if (kpIdx >= 0) bkp = body.substring(kpIdx + 5).toDouble();
                            if (kiIdx >= 0) bki = body.substring(kiIdx + 5).toDouble();
                            if (kdIdx >= 0) bkd = body.substring(kdIdx + 5).toDouble();
                            if (boostIdx >= 0) bboost = body.substring(boostIdx + 8).toInt();
                            
                            setBrewPIDTunings(bkp, bki, bkd, bboost);
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println("{\"status\":\"ok\"}");
                            Serial.printf("[WEB] Brew settings updated: Kp=%.1f, Ki=%.2f, Kd=%.1f, Boost=%ds\n",
                                         bkp, bki, bkd, bboost);
                        }
                        else if (requestLine.indexOf("POST /api/resetBrewSettings") >= 0) {
                            resetBrewPIDToDefaults();
                            
                            double bkp, bki, bkd;
                            int bboost;
                            getBrewPIDTunings(bkp, bki, bkd, bboost);
                            
                            String json = "{";
                            json += "\"status\":\"ok\",";
                            json += "\"kp\":" + String(bkp, 1) + ",";
                            json += "\"ki\":" + String(bki, 2) + ",";
                            json += "\"kd\":" + String(bkd, 1) + ",";
                            json += "\"boost\":" + String(bboost);
                            json += "}";
                            
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-Type: application/json");
                            client.println("Connection: close");
                            client.println();
                            client.println(json);
                            Serial.println("[WEB] Brew settings reset to defaults");
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
                            Serial.println("[WEB] Request: " + requestLine);
                        }
                        currentLine = "";
                    }
                } else if (c != '\r') {
                    currentLine += c;
                }
            }
        }
        
        delay(1);
        client.stop();
        Serial.println("[WEB] Client disconnected");
    }
}
