/**
 * script.js — Static mockup version
 *
 * All /api/* fetch calls are intercepted by mockFetch() so the page
 * works fully in a browser without a live ESP32.  Simulated temperature
 * drifts slowly around the set-point so the chart looks realistic.
 */

// ─── Mock state (mirrors ESP32 defaults) ────────────────────────────────────
const mockState = {
    currentTemp: 91.0,
    setTemp: 93.0,
    pidOutput: 380,
    error: false,
    brewMode: false,
    brewBoostPhase: false,
    brewDelayPhase: false,
    relayForceOff: false,
};

const mockPID = { kp: 75.0, ki: 1.5, kd: 0.0 };

const mockBrewPID = {
    kp: 50.0, ki: 0.5, kd: 8.0,
    boostSeconds: 5, boostDuty: 100,
    delaySeconds: 5, delayDuty: 0,
};

// Slowly drift temperature toward setTemp with a little noise
function tickMockTemp() {
    const error = mockState.setTemp - mockState.currentTemp;
    mockState.currentTemp += error * 0.03 + (Math.random() - 0.5) * 0.12;
    mockState.currentTemp = Math.round(mockState.currentTemp * 10) / 10;
    mockState.pidOutput = Math.max(0, Math.min(1000, error * mockPID.kp));
}

// ─── Mock fetch interceptor ──────────────────────────────────────────────────
async function mockFetch(url, options = {}) {
    tickMockTemp();
    const method = (options.method || 'GET').toUpperCase();
    const body = options.body ? JSON.parse(options.body) : {};

    const respond = (data) => ({
        ok: true,
        json: async () => data,
    });

    if (url === '/api/data') {
        const duty = mockState.relayForceOff ? 0 : (mockState.pidOutput / 1000 * 100);
        return respond({
            currentTemp: mockState.currentTemp,
            setTemp: mockState.setTemp,
            pidOutput: mockState.pidOutput,
            dutyCycle: Math.round(duty * 10) / 10,
            error: mockState.error,
            brewMode: mockState.brewMode,
            brewBoostPhase: mockState.brewBoostPhase,
            brewDelayPhase: mockState.brewDelayPhase,
            relayForceOff: mockState.relayForceOff,
        });
    }

    if (url === '/api/getPID') {
        return respond({ kp: mockPID.kp, ki: mockPID.ki, kd: mockPID.kd, target: mockState.setTemp });
    }

    if (url === '/api/setPID' && method === 'POST') {
        Object.assign(mockPID, { kp: body.kp, ki: body.ki, kd: body.kd });
        mockState.setTemp = body.target;
        return respond({ status: 'ok' });
    }

    if (url === '/api/resetPID' && method === 'POST') {
        Object.assign(mockPID, { kp: 75.0, ki: 1.5, kd: 0.0 });
        mockState.setTemp = 93.0;
        return respond({ status: 'ok', kp: mockPID.kp, ki: mockPID.ki, kd: mockPID.kd, target: mockState.setTemp });
    }

    if (url === '/api/resetPIDMemory' && method === 'POST') {
        return respond({ status: 'ok' });
    }

    if (url === '/api/getBrewSettings') {
        return respond({ ...mockBrewPID });
    }

    if (url === '/api/setBrewSettings' && method === 'POST') {
        Object.assign(mockBrewPID, body);
        return respond({ status: 'ok' });
    }

    if (url === '/api/resetBrewSettings' && method === 'POST') {
        Object.assign(mockBrewPID, { kp: 50.0, ki: 0.5, kd: 8.0, boostSeconds: 5, boostDuty: 100, delaySeconds: 5, delayDuty: 0 });
        return respond({ status: 'ok', ...mockBrewPID });
    }

    if (url === '/api/setRelayForce' && method === 'POST') {
        mockState.relayForceOff = body.forceOff;
        return respond({ status: 'ok', forceOff: mockState.relayForceOff });
    }

    return respond({ status: 'unknown endpoint' });
}

// Replace the global fetch with the mock when running from file://
const isLocalMock = (location.protocol === 'file:' || location.hostname === 'localhost');
const apiFetch = isLocalMock ? mockFetch : fetch.bind(window);

// ─── Chart setup ─────────────────────────────────────────────────────────────
let currentTemp = 92.5, setTemp = 93.0, pidOutput = 450, dutyCycle = 45.0;

if (typeof Chart === 'undefined') { console.error('Chart.js not loaded!'); }
const ctx = document.getElementById('tempChart').getContext('2d');
const chart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Current Temp (°C)', data: [], borderColor: '#103715',
            backgroundColor: 'rgba(16, 55, 21, 0.15)', borderWidth: 2,
            tension: 0.4, fill: true, pointRadius: 0,
        }, {
            label: 'Set Temp (°C)', data: [], borderColor: '#b84040',
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
                ticks: { color: 'rgba(182, 146, 110, 0.5)', callback: v => v + '°C' },
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
    document.querySelector('#dutyCycle .value').textContent = dutyCycle.toFixed(1);

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
    apiFetch('/api/data').then(r => r.json()).then(data => {
        currentTemp = data.currentTemp;
        setTemp = data.setTemp;
        pidOutput = data.pidOutput;
        dutyCycle = data.dutyCycle;
        dashboardAPI.setRelayError(data.error);
        updateBrewStatus(data.brewMode, data.brewBoostPhase, data.brewDelayPhase);
        updateRelayForceBtn(data.relayForceOff);
        updateMachineStatus(data.relayForceOff, data.brewMode);
        updateDashboard();
    }).catch(e => console.error('Fetch error:', e));
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
        label.textContent = 'Brew boost — heater at full power';
    } else if (brewMode && delayPhase) {
        badge.textContent = 'DELAY'; badge.className = 'brew-status-indicator delay';
        label.textContent = 'Pre-brew delay — heater off, brew PID pending';
    } else if (brewMode) {
        badge.textContent = 'BREWING'; badge.className = 'brew-status-indicator active';
        label.textContent = 'Brew PID active — maintaining extraction temperature';
    } else {
        badge.textContent = 'INACTIVE'; badge.className = 'brew-status-indicator';
        label.textContent = 'Brew mode off — press GPIO\u00a00 button to activate';
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

function toggleRelayForceOff() {
    const btn = document.getElementById('relayForceOffBtn');
    const currentlyForced = btn.classList.contains('forced-off');
    apiFetch('/api/setRelayForce', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ forceOff: !currentlyForced }),
    }).then(r => r.json()).then(d => {
        updateRelayForceBtn(d.forceOff);
    }).catch(e => { console.error('Relay force error:', e); alert('Failed to toggle relay'); });
}

function applyAllPID(btn) {
    applyPIDSettings(btn);
    applyBrewSettings(btn);
}

function resetAllPID(btn) {
    if (!confirm('Reset all PID values (heating + brewing) to factory defaults?')) return;
    resetPIDSettings({ textContent: '', style: {} });
    resetBrewSettings({ textContent: '', style: {} });
    const txt = btn.textContent;
    btn.textContent = 'Reset Complete ✓';
    setTimeout(() => { btn.textContent = txt; }, 2000);
}

function applyTargetTemp(btn) {
    const target = parseFloat(document.getElementById('targetInput').value);
    if (isNaN(target) || target < 0 || target > 120) { alert('Temp: 0–120°C'); return; }
    const kp = parseFloat(document.getElementById('kpInput').value);
    const ki = parseFloat(document.getElementById('kiInput').value);
    const kd = parseFloat(document.getElementById('kdInput').value);
    apiFetch('/api/setPID', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ kp, ki, kd, target }),
    }).then(r => r.json()).then(() => {
        const txt = btn.textContent;
        btn.textContent = 'Set ✓'; btn.style.backgroundColor = '#103715';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Set temp error:', e); alert('Failed to set temperature'); });
}

function applyPIDSettings(btn) {
    const kp = parseFloat(document.getElementById('kpInput').value);
    const ki = parseFloat(document.getElementById('kiInput').value);
    const kd = parseFloat(document.getElementById('kdInput').value);
    const target = parseFloat(document.getElementById('targetInput').value);
    if (isNaN(kp) || isNaN(ki) || isNaN(kd) || isNaN(target)) { alert('Invalid input'); return; }
    if (kp < 0 || kp > 500) { alert('Kp: 0–500'); return; }
    if (ki < 0 || ki > 50) { alert('Ki: 0–50'); return; }
    if (kd < 0 || kd > 2000) { alert('Kd: 0–2000'); return; }
    if (target < 0 || target > 120) { alert('Temp: 0–120°C'); return; }
    apiFetch('/api/setPID', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ kp, ki, kd, target }),
    }).then(r => r.json()).then(() => {
        const txt = btn.textContent;
        btn.textContent = 'Applied ✓'; btn.style.backgroundColor = '#28a745';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Update error:', e); alert('Failed to update PID'); });
}

function resetPIDMemory(btn) {
    apiFetch('/api/resetPIDMemory', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
    }).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            const txt = btn.textContent;
            btn.textContent = 'Memory Cleared ✓';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('PID memory reset error:', e); alert('Failed to reset PID memory'); });
}

function resetPIDSettings(btn) {
    if (!confirm('Reset all PID settings to factory defaults?')) return;
    apiFetch('/api/resetPID', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
    }).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            document.getElementById('kpInput').value = d.kp;
            document.getElementById('kiInput').value = d.ki;
            document.getElementById('kdInput').value = d.kd;
            document.getElementById('targetInput').value = d.target;
            const txt = btn.textContent;
            btn.textContent = 'Reset Complete ✓';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('Reset error:', e); alert('Failed to reset PID'); });
}

function applyBrewSettings(btn) {
    const kp = parseFloat(document.getElementById('brewKpInput').value);
    const ki = parseFloat(document.getElementById('brewKiInput').value);
    const kd = parseFloat(document.getElementById('brewKdInput').value);
    const boostSeconds = parseInt(document.getElementById('brewBoostSecondsInput').value);
    const boostDuty = parseInt(document.getElementById('brewBoostDutyInput').value);
    const delaySeconds = parseInt(document.getElementById('brewDelaySecondsInput').value);
    const delayDuty = parseInt(document.getElementById('brewDelayDutyInput').value);
    if (isNaN(kp) || isNaN(ki) || isNaN(kd) || isNaN(boostSeconds) || isNaN(boostDuty) || isNaN(delaySeconds) || isNaN(delayDuty)) { alert('Invalid input'); return; }
    if (kp < 0 || kp > 500) { alert('Brew Kp: 0–500'); return; }
    if (ki < 0 || ki > 50) { alert('Brew Ki: 0–50'); return; }
    if (kd < 0 || kd > 2000) { alert('Brew Kd: 0–2000'); return; }
    if (boostSeconds < 0 || boostSeconds > 30) { alert('Boost: 0–30s'); return; }
    if (boostDuty < 0 || boostDuty > 100) { alert('Boost duty: 0–100%'); return; }
    if (delaySeconds < 0 || delaySeconds > 30) { alert('Delay: 0–30s'); return; }
    if (delayDuty < 0 || delayDuty > 100) { alert('Delay duty: 0–100%'); return; }
    apiFetch('/api/setBrewSettings', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ kp, ki, kd, boostSeconds, boostDuty, delaySeconds, delayDuty }),
    }).then(r => r.json()).then(() => {
        const txt = btn.textContent;
        btn.textContent = 'Applied ✓'; btn.style.backgroundColor = '#28a745';
        setTimeout(() => { btn.textContent = txt; btn.style.backgroundColor = ''; }, 2000);
    }).catch(e => { console.error('Brew update error:', e); alert('Failed to update brew settings'); });
}

function resetBrewSettings(btn) {
    if (!confirm('Reset brew settings to factory defaults?')) return;
    apiFetch('/api/resetBrewSettings', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
    }).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            document.getElementById('brewKpInput').value = d.kp;
            document.getElementById('brewKiInput').value = d.ki;
            document.getElementById('brewKdInput').value = d.kd;
            document.getElementById('brewBoostSecondsInput').value = d.boostSeconds;
            document.getElementById('brewBoostDutyInput').value = d.boostDuty;
            document.getElementById('brewDelaySecondsInput').value = d.delaySeconds;
            document.getElementById('brewDelayDutyInput').value = d.delayDuty;
            const txt = btn.textContent;
            btn.textContent = 'Reset Complete ✓';
            setTimeout(() => { btn.textContent = txt; }, 2000);
        }
    }).catch(e => { console.error('Brew reset error:', e); alert('Failed to reset brew settings'); });
}

function init() {
    console.log('Dashboard init' + (isLocalMock ? ' [MOCK MODE]' : ''));

    apiFetch('/api/getPID').then(r => r.json()).then(d => {
        document.getElementById('kpInput').value = d.kp;
        document.getElementById('kiInput').value = d.ki;
        document.getElementById('kdInput').value = d.kd;
        document.getElementById('targetInput').value = d.target;
    }).catch(e => console.error('Failed to load PID settings:', e));

    apiFetch('/api/getBrewSettings').then(r => r.json()).then(d => {
        document.getElementById('brewKpInput').value = d.kp;
        document.getElementById('brewKiInput').value = d.ki;
        document.getElementById('brewKdInput').value = d.kd;
        document.getElementById('brewBoostSecondsInput').value = d.boostSeconds;
        document.getElementById('brewBoostDutyInput').value = d.boostDuty;
        document.getElementById('brewDelaySecondsInput').value = d.delaySeconds;
        document.getElementById('brewDelayDutyInput').value = d.delayDuty;
    }).catch(e => console.error('Failed to load brew settings:', e));

    fetchRealData();
    setInterval(fetchRealData, 1000);

    setTimeout(() => {
        const badge = document.getElementById('statusBadge');
        badge.textContent = isLocalMock ? 'Mock Mode' : 'Connected';
        badge.classList.add('connected');
    }, 500);
}

window.addEventListener('load', init);

window.dashboardAPI = {
    updateValues: (t, s, p, d) => { currentTemp = t; setTemp = s; pidOutput = p; dutyCycle = d; updateDashboard(); },
    setConnectionStatus: (c) => {
        const b = document.getElementById('statusBadge');
        b.textContent = c ? 'Connected' : 'Disconnected';
        c ? b.classList.add('connected') : b.classList.remove('connected');
    },
    setRelayError: (e) => {
        const bar = document.getElementById('dutyBar');
        e ? bar.classList.add('error') : bar.classList.remove('error');
    },
};
