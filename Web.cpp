#include "Web.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(WEBSERVER_PORT);
static String    ipAddress = "0.0.0.0";

// =====================================================================
// FRONTEND  (style carried over verbatim from NewWebsiteIdea/style.css)
// =====================================================================
static const char STYLE_CSS[] PROGMEM = R"CSS(
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  background: linear-gradient(135deg, #010202 0%, #12130b 100%);
  background-attachment: fixed; min-height: 100vh; padding: 20px; color: #b6926e; }
.container { max-width: 1200px; margin: 0 auto; }
.container > .card { margin-bottom: 20px; }
header { background: #020404; border: 1px solid rgba(182,146,110,0.3); padding: 20px 30px;
  border-radius: 8px; margin-bottom: 20px; display: flex; justify-content: space-between;
  align-items: center; box-shadow: 0 8px 32px rgba(0,0,0,0.6); }
header h1 { font-size: 2em; color: #b6926e; }
.status-badge { padding: 8px 20px; border-radius: 12px; font-weight: bold; font-size: 0.9em;
  background: #2a0f0f; color: #b6926e; border: 1px solid rgba(182,146,110,0.25); animation: pulse 2s infinite; }
.status-badge.connected { background: #103715; border-color: rgba(182,146,110,0.4); animation: none; }
@keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.5; } }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px,1fr)); gap: 20px; margin-bottom: 20px; }
.card { background: #020404; border: 1px solid rgba(182,146,110,0.2); padding: 25px; border-radius: 8px;
  box-shadow: 0 8px 32px rgba(0,0,0,0.5); transition: transform 0.3s ease, box-shadow 0.3s ease, border-color 0.3s ease; }
.card:hover { transform: translateY(-4px); box-shadow: 0 12px 40px rgba(0,0,0,0.7); border-color: rgba(182,146,110,0.45); }
.card-title { font-size: 1em; color: rgba(182,146,110,0.6); margin-bottom: 15px; text-transform: uppercase;
  letter-spacing: 1px; font-weight: 600; }
.value-display { display: flex; align-items: baseline; justify-content: center; }
.value-display .value { font-size: 3em; font-weight: bold; color: #b6926e; margin-right: 10px; }
.value-display .unit { font-size: 1.5em; color: rgba(182,146,110,0.5); }
.control-grid-3 { display: grid; grid-template-columns: repeat(3,1fr); gap: 15px; }
.control-group { display: flex; flex-direction: column; gap: 8px; }
.control-group label { font-size: 0.9em; color: rgba(182,146,110,0.7); font-weight: 600; }
.control-group input { padding: 12px 15px; border: 1px solid rgba(182,146,110,0.3); border-radius: 5px;
  font-size: 1.1em; font-weight: bold; color: #b6926e; background: #010202; }
.control-group input:focus { outline: none; border-color: #b6926e; box-shadow: 0 0 0 3px rgba(182,146,110,0.1); }
.pid-section-label { font-size: 0.78em; text-transform: uppercase; letter-spacing: 1.5px;
  color: rgba(182,146,110,0.4); padding-bottom: 6px; border-bottom: 1px solid rgba(182,146,110,0.1); margin: 8px 0; }
.btn-primary, .btn-reset, .btn-emergency { padding: 15px 30px; color: #b6926e; border: 1px solid rgba(182,146,110,0.3);
  border-radius: 6px; font-size: 1.1em; font-weight: bold; cursor: pointer;
  transition: transform 0.2s ease, box-shadow 0.3s ease, border-color 0.3s ease; box-shadow: 0 4px 15px rgba(0,0,0,0.4); }
.btn-primary { background: linear-gradient(135deg, #103715 0%, #0a2410 100%); }
.btn-reset, .btn-emergency { background: linear-gradient(135deg, #2a0f0f 0%, #1a0808 100%); }
.btn-primary:hover, .btn-reset:hover, .btn-emergency:hover { transform: translateY(-2px); border-color: #b6926e; }
.btn-primary.active { border-color: #b6926e; box-shadow: 0 0 0 3px rgba(182,146,110,0.15); }
.status-display { font-size: 1.8em; font-weight: bold; text-align: center; padding: 10px 20px; color: #b6926e;
  border-radius: 12px; border: 1px solid rgba(182,146,110,0.25); background: rgba(182,146,110,0.06); }
.status-display.brewing { color: #4caf6a; border-color: rgba(76,175,106,0.4); background: rgba(76,175,106,0.08); }
.status-display.steam { color: #e8e8e8; border-color: rgba(232,232,232,0.35); background: rgba(232,232,232,0.06); }
.status-display.error { color: #b84040; border-color: rgba(184,64,64,0.4); background: rgba(184,64,64,0.08); animation: pulse 0.8s infinite; }
.diag-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
.diag-row { display: flex; justify-content: space-between; align-items: center; padding: 5px 0;
  border-bottom: 1px solid rgba(182,146,110,0.07); }
.diag-label { color: rgba(182,146,110,0.6); font-size: 0.88em; }
.diag-value { color: #b6926e; font-size: 0.9em; font-variant-numeric: tabular-nums; }
.diag-led { padding: 3px 12px; border-radius: 10px; font-size: 0.78em; font-weight: bold; }
.diag-led.off { background: rgba(182,146,110,0.06); color: rgba(182,146,110,0.35); border: 1px solid rgba(182,146,110,0.15); }
.diag-led.on { background: #261608; color: #b6926e; border: 1px solid rgba(182,146,110,0.5); }
.btn-row { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 15px; }
.btn-row > * { flex: 1; }
.lock-banner { background: #2a0f0f; border: 1px solid rgba(182,146,110,0.4); color: #b6926e; padding: 12px 20px;
  border-radius: 8px; margin-bottom: 20px; text-align: center; font-weight: bold; display: none; }
.locked input, .locked button.cfg { opacity: 0.4; pointer-events: none; }
footer { background: #020404; border: 1px solid rgba(182,146,110,0.2); padding: 15px; border-radius: 8px;
  text-align: center; margin-top: 20px; color: rgba(182,146,110,0.5); }
@media (max-width: 768px) { .grid { grid-template-columns: 1fr; } .control-grid-3 { grid-template-columns: 1fr; } }
)CSS";

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>QuickMill PID</title><link rel="stylesheet" href="/style.css"></head><body>
<div class="container">
  <header><h1>QuickMill PID Control</h1><div class="status-badge" id="badge">Connecting</div></header>
  <div class="lock-banner" id="lockBanner">Machine busy - settings are read-only until IDLE</div>

  <div class="card"><div class="card-title">Preset</div>
    <div class="btn-row" style="margin-top:0">
      <button class="btn-primary cfg" id="preset0" onclick="selectPreset(0)">Preset 1</button>
      <button class="btn-primary cfg" id="preset1" onclick="selectPreset(1)">Preset 2</button>
      <button class="btn-primary cfg" id="preset2" onclick="selectPreset(2)">Preset 3</button>
    </div></div>

  <div class="grid">
    <div class="card"><div class="card-title">Temperature</div>
      <div class="value-display"><span class="value" id="temp">--</span><span class="unit">&deg;C</span></div></div>
    <div class="card"><div class="card-title">Target</div>
      <div class="value-display"><span class="value" id="target">--</span><span class="unit">&deg;C</span></div></div>
    <div class="card"><div class="card-title">Pressure</div>
      <div class="value-display"><span class="value" id="pressure">--</span><span class="unit">Bar</span></div></div>
    <div class="card"><div class="card-title">Heater Duty</div>
      <div class="value-display"><span class="value" id="duty">--</span><span class="unit">%</span></div></div>
    <div class="card" style="grid-column: span 2;"><div class="card-title">Status</div>
      <div class="status-display" id="status">--</div>
      <div style="margin-top:10px;text-align:center;color:rgba(182,146,110,0.6)" id="substate"></div></div>
  </div>

  <div class="card"><div class="card-title">PID Parameters</div>
    <div class="pid-section-label">Heating</div>
    <div class="control-grid-3">
      <div class="control-group"><label>Kp</label><input type="number" id="heatingKp" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Ki</label><input type="number" id="heatingKi" step="0.01" class="cfg"></div>
      <div class="control-group"><label>Kd</label><input type="number" id="heatingKd" step="1" class="cfg"></div></div>
    <div class="pid-section-label">Brewing</div>
    <div class="control-grid-3">
      <div class="control-group"><label>Kp</label><input type="number" id="brewKp" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Ki</label><input type="number" id="brewKi" step="0.01" class="cfg"></div>
      <div class="control-group"><label>Kd</label><input type="number" id="brewKd" step="1" class="cfg"></div></div>
    <div class="btn-row"><button class="btn-primary cfg" onclick="applyPid()">Apply PID</button></div></div>

  <div class="card"><div class="card-title">Temperature &amp; Steam</div>
    <div class="control-grid-3">
      <div class="control-group"><label>Sensor offset (&deg;C)</label><input type="number" id="tempOffset" step="0.5" class="cfg"></div>
      <div class="control-group"><label>Steam target (&deg;C)</label><input type="number" id="steamTarget" step="0.5" class="cfg"></div>
      <div class="control-group"><label>&nbsp;</label><button class="btn-primary cfg" onclick="applyTempSteam()">Apply</button></div></div></div>

  <div class="card"><div class="card-title">Safety Limits</div>
    <div class="control-grid-3">
      <div class="control-group"><label>Coffee max (&deg;C)</label><input type="number" id="coffeeTempMax" step="1" class="cfg"></div>
      <div class="control-group"><label>Steam max (&deg;C)</label><input type="number" id="steamTempMax" step="1" class="cfg"></div>
      <div class="control-group"><label>Pressure max (Bar)</label><input type="number" id="safePressureMax" step="0.5" class="cfg"></div></div>
    <div class="btn-row"><button class="btn-primary cfg" onclick="applySafety()">Apply Limits</button></div></div>

  <div class="card"><div class="card-title">Brew Timing (active preset)</div>
    <div class="control-grid-3">
      <div class="control-group"><label>Coffee target (&deg;C)</label><input type="number" id="coffeeTarget" step="0.5" class="cfg"></div>
      <div class="control-group"><label>Pre-infuse max (s)</label><input type="number" id="preinfuse" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Pre-infuse target (Bar)</label><input type="number" id="preinfuseBar" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Bloom (s)</label><input type="number" id="bloom" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Preheat (s)</label><input type="number" id="preheat" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Brew boost (s)</label><input type="number" id="brewMax" step="0.1" class="cfg"></div>
      <div class="control-group"><label>Shot time total (s)</label><input type="number" id="shot" step="1" class="cfg"></div></div>
    <div class="btn-row"><button class="btn-primary cfg" onclick="applyTiming()">Apply Timing</button></div></div>

  <div class="card"><div class="card-title">Diagnostics</div>
    <div class="diag-grid">
      <div><div class="pid-section-label">Inputs</div>
        <div class="diag-row"><span class="diag-label">Switch voltage</span><span class="diag-value" id="dSwV">--</span></div>
        <div class="diag-row"><span class="diag-label">Pressure voltage</span><span class="diag-value" id="dPrV">--</span></div>
        <div class="diag-row"><span class="diag-label">Steam switch</span><span class="diag-led off" id="dSteam">OFF</span></div>
        <div class="diag-row"><span class="diag-label">Coffee switch</span><span class="diag-led off" id="dCoffee">OFF</span></div>
        <div class="diag-row"><span class="diag-label">Temp sensor</span><span class="diag-led on" id="dTempOk">OK</span></div></div>
      <div><div class="pid-section-label">Outputs</div>
        <div class="diag-row"><span class="diag-label">Heater mode</span><span class="diag-value" id="dHeater">--</span></div>
        <div class="diag-row"><span class="diag-label">Pump</span><span class="diag-led off" id="dPump">OFF</span></div>
        <div class="diag-row"><span class="diag-label">Valve</span><span class="diag-led off" id="dValve">OFF</span></div>
        <div class="diag-row"><span class="diag-label">Brew timer</span><span class="diag-value" id="dTimer">--</span></div></div></div></div>

  <div class="card"><div class="card-title">System</div>
    <div class="btn-row" style="margin-top:0">
      <button class="btn-emergency cfg" id="muteBtn" onclick="toggleMute()">Mute</button>
      <button class="btn-reset cfg" onclick="resetAll()">Reset All Settings</button></div></div>

  <footer>ESP32-S3 PID | CoffeePID</footer>
</div>
<script src="/script.js"></script></body></html>
)HTML";

static const char SCRIPT_JS[] PROGMEM = R"JS(
const $=id=>document.getElementById(id);
let idle=true;
async function loadSettings(){
  const s=await (await fetch('/api/settings')).json();
  $('heatingKp').value=s.heatingKp; $('heatingKi').value=s.heatingKi; $('heatingKd').value=s.heatingKd;
  $('brewKp').value=s.brewKp; $('brewKi').value=s.brewKi; $('brewKd').value=s.brewKd;
  $('tempOffset').value=s.tempOffset; $('steamTarget').value=s.steamTarget;
  $('coffeeTempMax').value=s.coffeeTempMax; $('steamTempMax').value=s.steamTempMax; $('safePressureMax').value=s.safePressureMax;
  $('coffeeTarget').value=s.coffeeTarget;
  $('preinfuse').value=(s.preinfuseMs/1000).toFixed(1); $('preinfuseBar').value=s.preinfuseBar;
  $('bloom').value=(s.bloomMs/1000).toFixed(1); $('preheat').value=(s.preheatMs/1000).toFixed(1);
  $('brewMax').value=(s.brewMaxMs/1000).toFixed(1); $('shot').value=Math.round(s.shotMs/1000);
  for(let i=0;i<3;i++) $('preset'+i).classList.toggle('active', i===s.activePreset);
  $('muteBtn').textContent=s.buzzerMute?'Unmute':'Mute';
}
async function post(obj){
  await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)});
  loadSettings();
}
function applyPid(){ post({heatingKp:+$('heatingKp').value,heatingKi:+$('heatingKi').value,heatingKd:+$('heatingKd').value,
  brewKp:+$('brewKp').value,brewKi:+$('brewKi').value,brewKd:+$('brewKd').value}); }
function applyTempSteam(){ post({tempOffset:+$('tempOffset').value,steamTarget:+$('steamTarget').value}); }
function applySafety(){ post({coffeeTempMax:+$('coffeeTempMax').value,steamTempMax:+$('steamTempMax').value,safePressureMax:+$('safePressureMax').value}); }
function applyTiming(){ post({coffeeTarget:+$('coffeeTarget').value,preinfuseMs:Math.round($('preinfuse').value*1000),
  preinfuseBar:+$('preinfuseBar').value,bloomMs:Math.round($('bloom').value*1000),preheatMs:Math.round($('preheat').value*1000),
  brewMaxMs:Math.round($('brewMax').value*1000),shotMs:Math.round($('shot').value*1000)}); }
async function selectPreset(i){ await fetch('/api/preset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i})}); loadSettings(); }
function toggleMute(){ post({buzzerMute: $('muteBtn').textContent==='Mute'}); }
async function resetAll(){ if(!confirm('Reset all settings to defaults?'))return; await fetch('/api/reset',{method:'POST'}); loadSettings(); }
function led(el,on,onText,offText){ el.className='diag-led '+(on?'on':'off'); el.textContent=on?onText:offText; }
async function poll(){
  try{
    const d=await (await fetch('/api/state')).json();
    $('temp').textContent=d.temp.toFixed(1); $('target').textContent=d.target.toFixed(1);
    $('pressure').textContent=d.pressure.toFixed(2); $('duty').textContent=Math.round(d.duty/10);
    const st=$('status'); st.textContent=d.machine; st.className='status-display';
    if(d.machine==='Coffee') st.classList.add('brewing'); else if(d.machine==='Steam') st.classList.add('steam'); else if(d.machine==='Error') st.classList.add('error');
    $('substate').textContent = d.machine==='Error'? d.error : d.substate;
    $('dSwV').textContent=d.switchV.toFixed(2)+' V'; $('dPrV').textContent=d.pressureV.toFixed(2)+' V';
    led($('dSteam'),d.swSteam,'ON','OFF'); led($('dCoffee'),d.swCoffee,'ON','OFF'); led($('dTempOk'),!d.tempErr,'OK','FAULT');
    $('dHeater').textContent=d.heater; led($('dPump'),d.pump,'ON','OFF'); led($('dValve'),d.valve,'ON','OFF');
    $('dTimer').textContent=(d.brewTimer/1000).toFixed(1)+' s';
    const b=$('badge'); b.textContent='Connected'; b.classList.add('connected');
    idle = (d.machine==='Idle');
    $('lockBanner').style.display = idle?'none':'block';
    document.body.classList.toggle('locked', !idle);
  }catch(e){ const b=$('badge'); b.textContent='Disconnected'; b.classList.remove('connected'); }
}
loadSettings(); poll(); setInterval(poll,1000);
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

// =====================================================================
// Handlers
// =====================================================================
static void handleIndex() { server.send_P(200, "text/html", INDEX_HTML); }
static void handleCss()   { server.send_P(200, "text/css",  STYLE_CSS); }
static void handleJs()    { server.send_P(200, "application/javascript", SCRIPT_JS); }

static void handleState() {
    SystemState s = stateSnapshot();
    String j = "{";
    j += "\"temp\":"      + String(s.currentTemperature, 1);
    j += ",\"target\":"   + String(s.currentTargetTemperature, 1);
    j += ",\"pressure\":" + String(s.currentPressure, 2);
    j += ",\"duty\":"     + String((int)s.heatingPidOutput);
    j += ",\"machine\":\""    + String(machineStateText(s.machineState)) + "\"";
    j += ",\"substate\":\""   + String(coffeeSubstateText(s.coffeeSubstate)) + "\"";
    j += ",\"heater\":\""     + String(heaterModeText(s.heaterMode)) + "\"";
    j += ",\"error\":\""      + String(errorReasonText(s.errorReason)) + "\"";
    j += ",\"pump\":"  + String(s.pumpState ? "true" : "false");
    j += ",\"valve\":" + String(s.valveState ? "true" : "false");
    j += ",\"swSteam\":"  + String(s.switchSteam ? "true" : "false");
    j += ",\"swCoffee\":" + String(s.switchCoffee ? "true" : "false");
    j += ",\"tempErr\":"  + String(s.temperatureSensorError ? "true" : "false");
    j += ",\"switchV\":"   + String(s.switchVoltage, 2);
    j += ",\"pressureV\":" + String(s.pressureVoltage, 2);
    j += ",\"brewTimer\":" + String(s.brewTimerElapsedMs);
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
