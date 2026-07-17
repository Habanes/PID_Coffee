# CoffeePID - Notes / TODO

## Features to build

### Eco mode (design finalized 2026-07-17, IMPLEMENTED 2026-07-17)
New first-class `STATE_ECO` in the `MachineState` enum, alongside
IDLE/COFFEE/STEAM/HOT_WATER/ERROR - not a variant flag on IDLE (that was
the initial idea, superseded once the safety implications were worked
through: entering a brew/steam/hot-water directly from a cold eco state
would be a bad/unsafe outcome, so it needed its own state with its own
transition rules rather than silently inheriting IDLE's switch-handling).

**Entry** (from IDLE only): after `ecoTimeoutMs` (see settings below) has
elapsed continuously since the machine last entered IDLE. The timer
(`idleEntryMs`, a function-local static in `BrewStateMachine.cpp`, same
pattern as the existing `brewStartMs`/`substateEntryMs`) resets ONLY when
`machineState` transitions INTO `STATE_IDLE` from any other state (COFFEE
done, STEAM/HOT_WATER switch released, ERROR cleared, ECO woken) or at
boot (`setupBrew()`). Deliberately simple: browsing the on-device menu
while already idle does NOT reset the countdown, only actually leaving and
re-entering IDLE does. If 6+ minutes of menu-browsing without leaving IDLE
turns out to be a real annoyance in practice, revisit; not solved
preemptively.

**Exit**: ONLY via rotary encoder movement or button press - never via the
coffee/steam switches. Requires a new cross-core signal since the encoder/
button are read by `Input.cpp` on Core 0 (`UiTask`) but `machineState`'s
sole writer is the brew SM on Core 1 (`ControlTask`). New `SystemState`
field `bool ecoWakeRequested` (writer: Input process - set true on ANY
encoder delta or button edge while `machineState == STATE_ECO`; while in
ECO, `Input.cpp` does nothing else - no view cycling, no settings edits,
matching "we don't need to input anything except the wake signal").
Consumer: the brew SM, which performs the actual `STATE_ECO -> STATE_IDLE`
transition on its next 100ms cycle and clears the flag. Up to ~100ms
latency between the physical input and the actual wake; imperceptible.

**Switch activation while in ECO goes directly to the matching mode - REVISED
2026-07-17, simpler than the original design.** `swCoffee`/`swSteam`/both
while `machineState == STATE_ECO` transitions straight to
COFFEE/STEAM/HOT_WATER respectively, same switch-driven pattern as
HOT_WATER's own exit logic (see Hot Water mode above) - no gate, no
intermediate IDLE hop, no new `ErrorReason`. Original design routed this
through a new `ERR_SWITCH_FROM_ECO` error as a UX gate forcing IDLE first;
dropped as unnecessary complexity - the user is assumed capable of not
brewing at the 60-degree eco target if they don't want to, same trust
model the rest of the switch-driven transitions already use (e.g. nothing
stops starting a brew moments after a STEAM switch-release either).

**Outputs** (`driveOutputs()`): identical to `STATE_IDLE`'s case
(`heater = HEATER_PID`, `pump = false`, `valve = false`) - only the PID's
TARGET temperature differs (`ecoTargetTemp` instead of the active preset's
`coffeeTargetTemp`), set in the existing `target` computation alongside
the STEAM/COFFEE/HOT_WATER branches.

**Safety limit**: reuses the existing mode-aware selector unchanged -
`STATE_ECO` isn't `STATE_COFFEE`, so it falls into the lenient
`steamTempMax` branch, same as IDLE/STEAM/HOT_WATER. No new limit needed.

**New settings** (global, not per-preset - eco is about the machine
overall, not tied to which brew preset is active):
  - `ecoTargetTemp` (float): default 60 degrees C, range 30-80 degrees C.
    (30 = practical floor, no real benefit going lower; 80 = ceiling still
    meaningfully below the ~93-100 degree brew target range.)
  - `ecoTimeoutMs` (uint32_t): default 5 minutes (300000 ms), range
    1-60 minutes (60000-3600000 ms). Displayed/edited in MINUTES on the
    web GUI (converted to ms on submit), matching how the Brew Timing card
    already displays ms fields in seconds.
  Both new `Settings` struct fields + new NVS keys (e.g. "eco", "ecoT") +
  new `DEFAULT_*`/`*_MIN`/`*_MAX` constants in `Config.h` + clamped in
  `sanitizeLocked()` like every other field.

**New web GUI card**: "Eco" - two fields (Eco target degC, Eco timeout
min), one Apply button, same structural pattern as the existing
"Temperature & Steam"/"Safety Limits" cards.

**7-seg display**: static (NOT blinking - blinking is reserved for things
needing attention; eco is an expected, hands-off background state) "ECO"
(E, C, O - O reuses the existing digit-0 pattern), full override of
whatever menu view was selected, same precedent as STEAM/ERROR/HOT_WATER.

**Web GUI status**: new `.status-display.eco` class, steel-blue/slate tone
(`color: #6b8a9e` family - deliberately muted/desaturated vs. every other
status color, since eco is a "resting" state, not an active one). Status
text "Eco"; brew-status label "Eco mode - cooled to save energy, press or
turn the dial to wake". `machineEnumText()` returns `"ECO"`.

**Buzzer**: silent on automatic ECO entry (it's a passive, unattended
transition, not a user action - could fire in the middle of the night).
On wake (ECO->IDLE via rotary/button), play `SND_CLICK` (the existing
short click already used for button presses) as input-acknowledgment
feedback - NOT the full mode-enter/exit jingle, which stays reserved for
COFFEE/STEAM/HOT_WATER (genuinely different "machine is now doing X"
transitions).

### Error message display rework (design finalized 2026-07-17, IMPLEMENTED 2026-07-17, cross-cutting - not eco-specific, decided while designing eco's new error case)
Implementation note: `ERR_SWITCH_FROM_ECO` (the fourth word originally
planned, "idLE") was dropped before implementation - see the revised Eco
mode design above, switches now exit ECO directly rather than erroring.
Only 3 words shipped: HIGH / PRESS / SEnSOr. Glyphs actually added were
`I`, `G`, `n` only (not `d`/`L` - those were only needed for the dropped
"idLE" word).
Currently ALL errors just show a blinking "Err" on the 7-seg with zero
detail (you have to check the web GUI to know which of over-temp/
over-pressure/sensor-fault/etc. actually tripped). Decided to fix this
generally, for every `ErrorReason`, using the same scrolling mechanism
`renderIpScroll()` already implements for the IP address view - not just
for the new eco-related error.

**No blink phase** - go straight to the scrolling message on ERROR entry
(the blinking "Err" screen is removed entirely, not kept as a first
phase).

**Short recognizable words, not codes** - a 7-segment display can only
render a limited legible alphabet (digits plus roughly
A,b,C,d,E,F,G,H,I,J,L,n,O,P,q,r,S,t,U,y; letters like M,W,V,X,K,Z are not
renderable or are ambiguous). Full English phrases are impossible, but
short real/near-real words fit and read far better than 2-letter codes:
  - `ERR_OVER_TEMP` -> "HIGH" (H,I,G,H)
  - `ERR_OVER_PRESSURE` -> "PRESS" (P,r,E,S,S) - needs ZERO new glyphs,
    every character already exists in the table.
  - `ERR_TEMP_SENSOR` -> "SEnSOr" (S,E,n,S,O,r)
  - `ERR_SWITCH_FROM_ECO` -> "idLE" (i,d,L,E) - spells the required
    ACTION ("go to idle first") rather than the cause, which reads more
    usefully than a cause-description would.

**New glyphs needed** in `Display.cpp`'s character table: `I`/`i`, `G`,
`n`, `d`, `L` (five additions; standard well-established 7-segment
patterns). `H` and `O` are already being added for the "too hot to brew"
and "Hot Water" work above and get reused here too.

### ErrorReason enum, net effect after Hot Water + Eco
No new `ErrorReason` value needed for Eco (see revised note above) - only
change is the earlier-decided removal of `ERR_BOTH_SWITCHES` (see Hot Water
mode section, and "2026-07-16 code review" finding list below). Net effect,
the enum goes from `{NONE, BOTH_SWITCHES, OVER_TEMP, OVER_PRESSURE,
TEMP_SENSOR}` to `{NONE, OVER_TEMP, OVER_PRESSURE, TEMP_SENSOR}`.

### Safety limits (design finalized 2026-07-17, IMPLEMENTED 2026-07-17)
Checked against current state first: `coffeeTempMax`, `steamTempMax`, and
`safePressureMax` already exist as persisted `Settings` fields, already
clamped via `sanitizeLocked()`, and already have a working "Safety Limits"
card on the web GUI (all three inputs + Apply button, POST-able via
`/api/settings`) - "adjustable in the GUI and persisted" is already true
today for these three.

The actual, narrow ask: change `DEFAULT_COFFEE_TEMP_MAX` in `Config.h`
from `110.0f` to `120.0f` (stays within the existing 100-140 range, no
bounds change needed). Nothing else changes.

Nice side effect: this is the same fix discussed informally during the
2026-07-16 code review as a manual GUI workaround for the ERROR-clear-loop
bug (findings #1+#2 - `BREW_READY_TEMP` at 115 sitting above
`coffeeTempMax`'s old default of 110). Making 120 the DEFAULT rather than
requiring a manual settings change means new/reset installs get safe
headroom above `BREW_READY_TEMP` automatically, without relying on
remembering to raise it by hand.

### Hot Water mode (design finalized 2026-07-16, IMPLEMENTED 2026-07-17)
Originally noted as "Tea Water": instead of evaluating both switches
pressed as an error, treat it as a fourth first-class operating mode - full
design below, decided via grill session, ready to implement.

**State machine.** New `STATE_HOT_WATER` added to the `MachineState` enum
(`State.h`), exactly as first-class as IDLE/COFFEE/STEAM - own case in
`driveOutputs()`, own text in `machineStateText()`, own case in the web
`machineEnumText()`, own display override. NOT a variant of ERROR - it's a
legitimate user-intended mode, so it gets its own state rather than
piggybacking on the error path.

**Entry.** `swSteam && swCoffee` transitions to `STATE_HOT_WATER` from ANY
state - including interrupting an active `COFFEE` or `STEAM` run mid-way
(abandons the current substate/timers immediately). This fully replaces
the old `ERR_BOTH_SWITCHES` trip; both-switches-active is no longer an
error under any circumstance.

**Exit.** Same pattern as STEAM's existing transitions: if only one switch
remains active, go directly to the corresponding mode (COFFEE or STEAM); if
neither is active, go to IDLE. No dedicated "release" gesture needed.

**Outputs** (`driveOutputs()`): `heater = HEATER_OFF` (no heating - the
whole point is flowing water through the block to cool it, not heat it),
`pump = true`, `valve = true` (energized/closed - this is the path that
routes pump output THROUGH the thermoblock and out the group head, same
routing COFFEE's brewing substates use; `valve = false` vents to drain
without going through the block, which would defeat the cooling purpose).

**Safety limits.** No new limit. Reuses the existing mode-aware selector
unchanged: `tempLimit = (ms == STATE_COFFEE) ? coffeeTempMax : steamTempMax`
- `STATE_HOT_WATER` falls into the `else` branch alongside IDLE/STEAM and
gets the lenient `steamTempMax` (~145 degrees), since hot water is expected
to run right after steaming when the block may still be hot - using the
tight `coffeeTempMax` (~110) here would immediately trip an error and
defeat the feature. `safePressureMax` and sensor-fault checks remain
blanket safety nets across all modes, unchanged. No new duration/timeout
limit either - runs exactly as long as both switches are held, same
unbounded-by-design model STEAM already uses; revisit only if overflow
turns out to be a real practical problem once used.

**No new persisted settings, no new NVS keys, no new GUI settings card.**
Fully switch-driven, like STEAM - nothing to configure.

**7-seg display** (`Display.cpp`): reuses `renderTemp()`, same as STEAM,
showing current block temperature (the number that matters while cooling
down) with a new leading glyph `CHAR_H` to distinguish it from STEAM's
dash-prefixed temperature view: `renderTemp(s.currentTemperature, CHAR_H)`.
`CHAR_H` also gets reused for the separate "too hot to brew" HOT indicator
(see review findings below) - same glyph, related concept, no conflict
since they're mutually exclusive states.

**Buzzer**: joins the existing active-mode jingle set - add
`STATE_HOT_WATER` to the `wasActive`/`nowActive` checks in
`BrewStateMachine.cpp` alongside COFFEE/STEAM, so entering/exiting from
IDLE plays the same ascending/descending perfect-fifth jingle. An
active-to-active handoff (e.g. COFFEE interrupted into HOT_WATER) stays
silent, consistent with how COFFEE<->STEAM handoffs already behave today.

**Web GUI**: new CSS class `.status-display.hotwater`, violet tone
(`color: #a06bd4; border-color: rgba(160, 107, 212, 0.4); background:
rgba(160, 107, 212, 0.08);`) - deliberately distinct from every existing
status color (cyan-blue preinfuse, green bloom/brewing, orange preheat,
red brewmax/emergency, blue done, white/gray steam, amber
heating/default). Status text "Hot Water"; brew-status label "Pump on -
flowing through thermoblock, heater off". `machineEnumText()` returns
`"HOT_WATER"`. No other GUI changes needed - the lock-banner/read-only
behavior for non-IDLE states already applies automatically.

**Cleanup**: `ERR_BOTH_SWITCHES` becomes fully unreachable once the entry
logic above replaces the old error branch (it was the ONLY place that ever
set it). Remove it from the `ErrorReason` enum in `State.h` and its case in
`errorReasonText()` - keeping an enum value that can structurally never
occur invites confusion later.

**Also needed at implementation time**: `DOCUMENTATION.txt` needs real
updates to match (section 1's mode list, section 6's transition/output
tables, section 7's safety model text, section 8's buzzer/display
behavior) - it's the project's single source of truth and currently only
describes 3 modes + ERROR.

### Presets (design finalized 2026-07-17, web-GUI fixes IMPLEMENTED 2026-07-17)
Original complaint: "presets currently do not work correctly." Grilled and
narrowed down to a specific, confirmed symptom: the preset selection
buttons in the web GUI "don't seem to do things."

Root cause identified: a feedback gap, not (necessarily) a functional bug.
Every other settings button (`applyPid`, `applyCoffeeTarget`,
`applyTempSteam`, `applySafety`, `applyTiming`) goes through a shared
`post(obj, btn)` helper in `Web.cpp`'s `SCRIPT_JS` that calls `flash(btn)`
on success - the button text changes to "Applied (checkmark)" for 1.5s.
`selectPreset(i)` was written differently and never calls `flash()` - the
only feedback is a subtle CSS `.active` class toggle (border/box-shadow)
on the preset buttons, easy to miss since one button is already marked
active by default. `loadSettings()` IS already called after a successful
preset switch and DOES already re-populate all dependent fields (quick
coffee target, full Brew Timing card, etc.) - so the underlying
switch-and-persist mechanism is believed to work; it just gives no
unambiguous confirmation that it worked.

Decided fixes (two, both to be done):
1. **Add `flash()` confirmation to `selectPreset()`**: pass the clicked
   button element through (`onclick="selectPreset(0, this)"`) and call
   `flash(btn)` alongside `loadSettings()`, matching every other button's
   pattern.
2. **Strengthen the `.active` class's visual weight**: currently just
   `border-color: #b6926e; box-shadow: 0 0 0 3px rgba(182, 146, 110,
   0.15);` - subtle on the dark theme. Add a background-color shift too
   (e.g. similar to `.btn-primary:hover`'s lighter gradient) so the
   currently-selected preset is unmistakable at a glance, not just on
   close inspection.

Separately (found during the original code review, still queued - see
"2026-07-16 code review" section, finding #3): `settingsSetActivePreset()`
in `Settings.cpp` skips `sanitizeLocked()`, unlike every other settings
mutator. Fix: add the missing `sanitizeLocked()` call before
`writeNvsLocked()`.

7-seg menu side (`VIEW_PRESET`, encoder-driven) already gives adequate
feedback today - immediate display re-render via `renderPreset()` plus an
audible `SND_TICK` on every encoder step - no change needed there.

If, after both web-GUI fixes and the `sanitizeLocked()` fix ship, presets
still seem not to work, re-open this investigation with more specific
symptoms (e.g. "the *values* within a preset are wrong" vs. "the
*selection* doesn't stick" vs. something else) - those would point at a
different, not-yet-found bug.

### Pressure sensor optional (design finalized 2026-07-17, IMPLEMENTED 2026-07-17)
Some machine variants don't have a pressure sensor. Add a config variable
that disables the pressure feature - all logic around pressure and the
pressure elements in the GUI.

**Toggle mechanism**: new compile-time `#define HAS_PRESSURE_SENSOR 1` in
`Config.h`, alongside the existing `PIN_PRESSURE`/`PRESSURE_*` hardware
defines - it IS a hardware config, not a user preference, so it belongs
there rather than as a persisted runtime `Settings` toggle. Flip to `0` and
reflash for a no-sensor build. Lowest-effort option: disabled branches
compile out entirely, dead GUI markup never ships in the binary, and no new
persisted state/NVS key/API surface is needed.

**Sensor read** (`ControlTask.cpp`): wrap the `setupPressure()` and
`readPressure()` calls in `#if HAS_PRESSURE_SENSOR ... #endif`. When
disabled, `state.currentPressure`/`state.pressureVoltage` simply stay at
their zero-initialized defaults forever - `Pressure.cpp` itself is left
untouched (still compiled in, just never called; negligible flash cost,
lower effort than also gutting that file).

**Safety trip logic** (`BrewStateMachine.cpp`): the over-pressure trip
(`overPressNow = pressure > safePressureMax`) and the ERROR-clear check
(`pressOk = pressure < safePressureMax`) both get `#if HAS_PRESSURE_SENSOR`
guards - forced to `false`/`true` respectively when disabled, so a
permanently-zero `currentPressure` can never trip an error and can never
block ERROR from clearing.

**Preinfuse->Bloom transition**: NO code change needed. The existing
condition is already `pressure >= p.preinfuseTargetBar || subElapsed >=
p.preinfuseMaxMs` - with pressure permanently 0 and any sane
`preinfuseTargetBar` > 0, the pressure side of the OR simply never
fires, and the substate advances purely on the existing `preinfuseMaxMs`
timeout. Degrades to time-only preinfuse automatically, for free.

**Settings fields left alone** (`safePressureMax`, `preinfuseTargetBar`):
NOT stripped from `Settings`/NVS/sanitize - low-effort call, they're
harmless-but-inert when disabled (never read in the trip logic above;
`preinfuseTargetBar` never satisfied per the point above). Only their GUI
exposure is trimmed (below), not the underlying storage.

**Web GUI** (`Web.cpp`), all gated behind `#if HAS_PRESSURE_SENSOR` /
`#endif` around the relevant chunks of the `PROGMEM` raw-string literals
(adjacent raw strings concatenate fine around a preprocessor block - same
technique as any other compile-time-optional HTML fragment):
  - The standalone "Pressure" live-value card (current card showing
    `pressureVal` in Bar) - removed entirely from the dashboard when
    disabled.
  - The `safePressureMax` field in the "Safety Limits" card's 3-column
    grid - dropped, leaving 2 of 3 cells filled (coffee max, steam max),
    same "leave the grid as-is" precedent as the tempOffset removal above.
  - The two diagnostics rows ("Pressure Pin Voltage", "Calculated
    Pressure").
  - Corresponding `SCRIPT_JS` lines: the `pressureVal`/diagnostic DOM
    updates, `safePressureMax` in `loadSettings()`, and `safePressureMax`
    in `applySafety()`'s posted object.
  - JSON payload: `pressure`/`pressureV` dropped from `handleState()`,
    `safePressureMax` dropped from both `handleState()` and
    `handleSettings()`.
  - **Not touched**: the Brew Timing card's "Pre-infuse target (Bar)"
    field (`preinfuseTargetBar`) stays visible even when disabled - it's
    embedded inline in a 4-field grid row (pre-infuse max / target-bar /
    bloom / preheat), so hiding it would require restructuring that row's
    layout. Left as an inert-but-harmless field per the "low-effort" brief
    above; revisit only if it turns out to actually confuse users on
    no-sensor builds.

**`DOCUMENTATION.txt`**: needs a note added (wherever the pressure
process/safety-limit tables live) that pressure support is a compile-time
option via `HAS_PRESSURE_SENSOR`, and what changes/degrades when it's off.

### Calibration offset removal (design finalized 2026-07-17, IMPLEMENTED 2026-07-17)
Remove the `tempOffset` feature completely - a per-machine calibration knob
(default 5 degrees C, range 0-10) subtracted from the EMA-filtered raw TSIC
reading in `Temperature.cpp` to compensate for the sensor reading hotter
than the water at the puck. Decided: not necessary, remove entirely rather
than keep as dead weight.

**Touch points (8 total):**
  - `Config.h`: remove `DEFAULT_TEMP_OFFSET`, `TEMP_OFFSET_MIN`,
    `TEMP_OFFSET_MAX`.
  - `Settings.h`: remove the `tempOffset` field from the `Settings` struct.
  - `Settings.cpp`: remove the `sanitizeLocked()` clamp, the NVS load
    (`prefs.getFloat("off", ...)`), the NVS save (`prefs.putFloat("off",
    ...)`), and the `resetSettings()` default assignment - four separate
    lines, one per lifecycle stage, same pattern as every other field.
  - `Temperature.cpp`: `readTemperature()` currently does
    `state.currentTemperature = filtered - offset;` after reading
    `settings.tempOffset` under `SETTINGS_LOCK()`. Simplifies to
    `state.currentTemperature = filtered;` directly, no settings lock
    needed at all in this function anymore.
  - `Temperature.h`: update the header comment ("Reads the TSIC, subtracts
    the calibration offset...") to drop the offset mention.
  - `Web.cpp` (3 spots): remove the `tempOffset` `<input>` from the
    "Temperature & Steam" card; remove `$('tempOffset').value = s.tempOffset`
    from `loadSettings()`; remove `tempOffset` from the object posted by
    `applyTempSteam()`; remove it from both `handleState()`'s JSON output
    and `handleSettings()`'s parse.
  - `DOCUMENTATION.txt`: remove the `tempOffset` row from the process/
    settings tables (section 1, section on defaults), drop "+ tempOffset"
    from the Temperature pipeline description, and reword the "Temperature
    is calibrated at the source" paragraph (section on data flow) since
    there's no calibration step anymore - `currentTemperature` is simply
    the EMA-filtered raw reading.

**GUI layout after removal**: the "Temperature & Steam" card's 3-column
grid row currently holds offset field + steam-target field + Apply button.
Dropping the offset div leaves 2 of 3 cells filled (steam-target field,
button) - grid markup and CSS left as-is, no restructuring; the empty
third cell is a non-issue. Card title unchanged ("Temperature & Steam")
even though only the steam-target field remains, to avoid a churny rename
for a single-field card.

**NVS**: the persisted key (`"off"`) is left orphaned on devices that
already saved a value - no migration/erase logic added. It's a few unused
bytes in flash, harmless, consistent with how the codebase doesn't clean up
other removed keys either.

### WiFi credentials editable via GUI (design finalized 2026-07-17, IMPLEMENTED 2026-07-17)
Originally noted as "wifi passwort einstellen über esp wifi". Today
`WIFI_SSID`/`WIFI_PASSWORD` are hardcoded in `Config.h`; `setupWeb()` tries
station mode with those compiled-in values for ~10s
(`WIFI_CONNECT_ATTEMPTS=20 * WIFI_CONNECT_DELAY_MS=500`), then falls back
to a hardcoded AP (`AP_SSID="QuickMill-PID"`, `AP_PASSWORD="changeme123"`).
Changing networks today means re-flashing.

**Target behavior**: on boot, read station SSID/password from NVS and try
to connect for up to ~1 minute. If that network isn't found or can't be
joined in time, fall back to a self-hosted AP - renamed to
`AP_SSID="PID_COFFEE"`, `AP_PASSWORD="ESPRESSO26"` (`Config.h` constants
updated). The full web GUI is already served identically in AP mode as in
station mode (no new code needed there) - so a user with no home WiFi at
all can just connect directly to `PID_COFFEE` and run the machine
entirely over the self-hosted AP, indefinitely, no external network ever
required. If they DO want the machine on their home network, they connect
to `PID_COFFEE` once and use a new GUI card to submit their real
SSID/password, which is what gets tried on the next boot.

**Boot timeout**: bump `WIFI_CONNECT_ATTEMPTS`/`WIFI_CONNECT_DELAY_MS` in
`Config.h` so the retry window totals ~60s instead of ~10s (e.g.
`WIFI_CONNECT_ATTEMPTS=60`, `WIFI_CONNECT_DELAY_MS=1000`) before falling
back to AP mode.

**Applying new credentials, deliberately minimal**: submitting the GUI
form only persists the new station SSID/password to NVS - nothing else
happens live. No `WiFi.disconnect()`/`WiFi.begin()` retry, no forced
reboot. The device keeps running exactly as it was (stays in AP mode if
that's where it already was) and only picks up the new stored credentials
naturally on the next restart (power cycle, crash recovery, future OTA,
etc.). Nothing to gate on `machineState == IDLE` either, since nothing
about the currently-running machine changes.

**Storage**: new `Preferences` instance/namespace owned by `Web.cpp`
itself (e.g. `"wifi"`, keys `"ssid"`/`"pass"`) - separate from the existing
`Settings.cpp`/`settings` NVS namespace, since WiFi credentials aren't a
machine-tuning `Settings` field. `setupWeb()` reads these at boot; if
absent (fresh install, nothing saved yet), falls back to the compile-time
`WIFI_SSID`/`WIFI_PASSWORD` from `Config.h` exactly as today - those
defines stay as the first-boot fallback, not removed.

**GUI**: new "WiFi" card on the main dashboard, same structural pattern as
every other settings card - SSID text field, password field
(`type="password"`), one Apply button. SSID field prefilled from the
currently-stored/active value (non-secret, fetched via a new field on the
existing settings-load response). Password field always left BLANK on
load - the stored password is never echoed back to the browser; whatever
is in the field when Apply is clicked is saved verbatim, no client-side
validation added (consistent with every other settings card here, none of
which validate client-side either). New `POST /api/wifi` handler, NOT
routed through `Settings.cpp`'s existing settings machinery - separate
NVS namespace, separate concern.

**Implementation note**: the WiFi card's inputs/button deliberately do NOT
get the `cfg` CSS class every other settings control uses - `cfg` is what
the existing `.locked input.cfg, .locked button.cfg { pointer-events: none;
}` rule targets to grey out controls whenever `machineState != IDLE`. Since
this feature is explicitly not IDLE-gated, giving the WiFi fields that class
would have silently fought the design (greyed out and inert during a brew,
even though nothing about saving prevents it). Caught during implementation,
not part of the original design write-up above.

**Not addressed, out of scope for this pass**: no mDNS/friendly-hostname
(e.g. `coffeepid.local`) - if the new network gives the device a different
IP after the next restart, a browser tab pointed at the old IP simply
stops working and the user has to rediscover the new IP (same
rediscovery step as today's existing AP/STA fallback switch already
requires). Worth a future look, not required for this feature to be
useful.

### Later / not now
- Update over the air (OTA)
- Recordings: record a shot (curve, values) and store it for later review


## Known bugs (not yet fixed)

### Brewing/steaming sometimes doesn't start; machine stays stuck in idle
Two independent contributors identified during the 2026-07-16 review, see
"2026-07-16 code review" below for detail:
  1. For COFFEE specifically: not actually a bug, but the BREW_READY_TEMP
     gate blocking entry with zero user feedback (fix decided - see below,
     "HOT" indicator).
  2. For COFFEE and STEAM both: Switches.cpp reads the switch ladder ADC
     with a single unaveraged analogRead() per cycle, unlike Pressure.cpp
     (which averages 4 samples specifically to tame ADC noise). The
     debounce timer resets on any single-cycle band flicker. If there's
     electrical noise right at a switch transition, this could prevent the
     600ms debounce from ever committing. NOT YET FIXED - needs real
     scope/logging data from the machine to confirm before changing
     debounce behavior blind. Candidate fix: average multiple analogRead()
     samples in Switches.cpp, mirroring Pressure.cpp's pattern.


## 2026-07-16 code review - findings and decisions

Full-codebase read-through + grill session. Each item below is a decision
already made. Status as of 2026-07-17: findings #1, #2, #3, #7, #8, #9 fixed;
#4 is a comment-only decision (also done); #5 and #6 remain deferred; #10
remains open (design finalized, not implemented).

### 1. FIXED 2026-07-17 - ERROR-clear check uses the wrong temperature limit
`BrewStateMachine.cpp` line ~147:
```
bool tempOk = temp < (steamTempMax - ERROR_CLEAR_HYSTERESIS);
```
This decides whether ERROR can clear back to IDLE, but it always compares
against `steamTempMax`, regardless of which limit (`coffeeTempMax` or
`steamTempMax`) actually caused the trip. If ERROR trips because
`coffeeTempMax` (110 degrees) was exceeded during a brew (e.g. temp = 112),
the clear check uses `steamTempMax - 5` = 140, which is already true at
112 - so it clears back to IDLE almost immediately, without a real
cool-down. This is the likely root cause of the "machine stops at 110
degrees with error" behavior reported in the field.

Decided fix (Option A): add a static `errorFromMode`, set it at the moment
of transition into ERROR (`if (newMs == STATE_ERROR && ms != STATE_ERROR)`,
alongside the existing `errorReason` assignment). Use it at clear-time to
pick the correct limit: `(errorFromMode == STATE_COFFEE) ? coffeeTempMax :
steamTempMax`. Small, self-contained change, matches the file's existing
style (function-local statics already used for debounce timers).

Rejected alternative: always clear against `coffeeTempMax` unconditionally
- simpler, but overly conservative for STEAM-mode trips (would force
cooling further than necessary after a steam overtemp).

### 2. FIXED 2026-07-17 - BREW_READY_TEMP (115) vs coffeeTempMax (110) - documentation only
`Config.h` has `BREW_READY_TEMP = 115.0f`, sitting above the default
`coffeeTempMax` (110) that trips ERROR during a brew - an inversion that,
combined with bug #1 above, produced the observed error-loop (start a brew
at 111-115 degrees post-steam, trip within 300ms, clear almost instantly
because of bug #1, re-enter, repeat).

Decision: NOT changing `Config.h`. You are raising `coffeeTempMax` yourself
as a runtime setting via the GUI (not the compiled default) to get clear
headroom above 115. Once bug #1 is fixed, the clear-check is correct
regardless of the exact values, so this is no longer a safety issue - just
a documentation one.

`DOCUMENTATION.txt` still says `BREW_READY_TEMP 98` (stale - predates the
115 change). FIX: update the doc to say 115, matching `Config.h`. Text-only
change, no behavior risk.

### 3. FIXED 2026-07-17 - settingsSetActivePreset() skips sanitizeLocked()
`Settings.cpp`: every other settings mutator (`settingsApply`,
`settingsAdjustCoffeeTarget`, `settingsAdjustShotTime`, `resetSettings`)
calls `sanitizeLocked()` before persisting. `settingsSetActivePreset()`
only inline-clamps the index and skips the full sanitize pass. A preset
saved before global limits were tightened could serve stale, technically
out-of-range values until something else triggers a sanitize.

Fix: add a `sanitizeLocked()` call before `writeNvsLocked()` in
`settingsSetActivePreset()`, matching the other three mutators. One line.

### 4. DECIDED AGAINST - centralizing IDLE-only write enforcement (comment added 2026-07-17)
Considered adding a `machineState == IDLE` check inside `Settings.cpp`'s
mutators themselves (instead of relying on each caller - `Web.cpp`
handlers, `Input.cpp` - to check independently). Rejected: this would give
`Settings.cpp` a dependency on `State.h`/`stateMutex`, inverting the
current clean one-directional dependency (State/BrewSM/Web depend on
Settings, not vice versa). Every actual call site today already checks
correctly. Instead: add a one-line comment on each mutator in `Settings.h`
noting "caller must ensure machineState == IDLE before calling." Not yet
done.

### 5. DEFERRED - coherence clamp silently overrides preset targets
`Settings.cpp`'s `sanitizeLocked()` forces e.g. `coffeeTargetTemp` down
whenever it would exceed `coffeeTempMax - 10`, with no indication to the
user that anything besides the field they touched changed. Real UX gap,
not a safety/correctness bug (resulting values are always safe). Deferred
to be handled alongside the eco-mode/preset GUI rework already planned
above, since that work touches the same settings UI anyway. Candidate
fixes discussed: (a) web GUI diff-checks the POST response and shows a
warning if a preset target got adjusted, or (b) reject the write instead of
silently clamping. Neither implemented.

### 6. DEFERRED - every encoder tick does a full NVS flash write
`settingsAdjustCoffeeTarget`, `settingsAdjustShotTime`, and
`settingsSetActivePreset` each call `writeNvsLocked()` synchronously on
every single call - dialing a value through several steps does one flash
write per detent. Real but slow-motion concern (flash write endurance;
possible brief `ControlTask` stall since the write holds `settingsMutex`
for "tens of ms"). Candidate fix: accept the edit into the in-RAM struct
immediately, defer the actual NVS write until N ms of no further encoder
activity (debounce-on-idle). Deferred, not implemented.

### 7. FIXED 2026-07-17 - web dashboard's Chart.js is CDN-loaded, breaks in AP-fallback mode
`Web.cpp`'s `INDEX_HTML` loads `https://cdn.jsdelivr.net/npm/chart.js`.
The device's AP-fallback mode (`QuickMill-PID` SSID) exists specifically
for when there's no WiFi network to join - i.e. no internet path either -
so in that exact mode the temperature history chart silently fails to
render (guarded, no crash, but no chart and no explanation).

Fix: vendor Chart.js locally - store a minified build as another PROGMEM
string in `Web.cpp` (same pattern as `STYLE_CSS`/`SCRIPT_JS`/`INDEX_HTML`),
serve it from a new `/chart.js` route, swap the `<script src>`. Chart.js
minified is roughly 200KB; trivial against the 3MB APP partition on the
N16R8 module, and it's PROGMEM/flash, not NVS, so it doesn't touch the
settings storage.

### 8. FIXED 2026-07-17 - long-press is a dead end outside VIEW_SET_COFFEE
`Input.cpp`: holding the button past `BTN_LONG_PRESS_MS` sets
`longHandled = true` in any view, but only `VIEW_SET_COFFEE` has an actual
long-press action (toggling whole-degree vs tenths editing). In every
other view, a hold does nothing on threshold-cross AND suppresses the
short-press view-cycle action on release - so holding the button in
`VIEW_TEMP`/`VIEW_TIMER`/`VIEW_PRESET`/`VIEW_IP` does literally nothing,
with no feedback, easily misread as broken hardware.

Decided fix: fall back to short-press behavior (cycle the view) whenever
the current view has no long-press action of its own, rather than giving
long-press a new job in those views. Smallest, most surgical change -
removes the dead end without introducing new UX to design.

### 9. FIXED 2026-07-17 - Buzzer seq[4] has no bounds check; naming note
`Buzzer.cpp`'s `Step seq[4]` is sized for today's largest sequence (2
steps, `SND_MODE_ENTER`/`SND_MODE_EXIT`). `buildSequence()` writes into it
with no bounds check - safe today, but a silent stack-array overflow
landmine for whoever adds a longer sequence later (e.g. a multi-note
jingle). Fix: bump the array size with headroom (e.g. 8) or add a bounds
check/static_assert in `buildSequence()`.

Separately (not a bug, just noted): `EMA_ALPHA` (temperature sensor
smoothing, `Temperature.cpp`) and `PID_EMA_FACTOR` (PID derivative
smoothing via the rancilio-fork's `SetSmoothingFactor()`) are two
unrelated coefficients that happen to share a default value (0.6). Easy to
conflate when tuning and grepping for "EMA". Decided: add a clarifying
comment rather than rename - both are already correctly scoped to
different Config.h sections, this is cosmetic only.

### 10. FIX - new feature: "too hot to brew" indicator
When `machineState == IDLE && switchCoffee == true && currentTemperature >
BREW_READY_TEMP`, the coffee switch is being silently ignored by the
BREW_READY_TEMP gate with zero feedback. Decided to add explicit feedback,
both places:

**7-seg (Display.cpp):** blinking "HOT" (`H O t`, blank 4th digit) - NOTE:
`renderErrorBlink()` (the blink-pattern precedent this cited) was removed by
the "Error message display rework" above (implemented 2026-07-17); reuse its
on/off blink-timer pattern from `renderSetCoffee()` instead, still available
as precedent. Fully overriding whatever menu view was selected - same
precedent as how
`STATE_STEAM`/`STATE_ERROR` already hard-override the IDLE menu-view
switch in `refreshDisplay()`. Needs one new glyph, `CHAR_H` (segments
b,c,e,f,g); `O` can reuse the existing `digitSeg[0]` pattern, no new glyph
needed there. Reverts to the normal menu view the instant the switch
releases or the temp drops enough to actually start brewing.

**Web GUI (Web.cpp):** `updateBrewStatus()` in `SCRIPT_JS` gets a new
branch for this condition, label: "Too hot to brew - waiting for block to
cool below {BREW_READY_TEMP} degrees C", styled with the existing
`.status-display.heating` class (no new CSS - this is a normal wait state,
not an error, shouldn't visually read as one). Requires adding
`BREW_READY_TEMP` to the `/api/state` JSON payload (`handleState()` in
Web.cpp) since it's currently a C++-only constant not exposed to the
frontend.

Explicitly out of scope for this fix: the switch-debounce/ADC-averaging
reliability question (see "Known bugs" section above) - not touched here,
needs real hardware data first.

### Weird coherence-clamp snippet (original note, resolved)
The original note flagged this snippet in `sanitizeLocked()` as "seems a
bit weird, investigate":
```
if (p.coffeeTargetTemp > settings.coffeeTempMax - 10.0f)
    p.coffeeTargetTemp = settings.coffeeTempMax - 10.0f;
if (p.steamTargetTemp > settings.steamTempMax - 10.0f)
    p.steamTargetTemp = settings.steamTempMax - 10.0f;
if (p.shotMs < p.brewMaxMs) p.shotMs = p.brewMaxMs;
```
Investigated: not a functional bug (the shotMs/brewMaxMs case can't
actually underflow past SHOT_TIME_MIN_MS given the current *_MIN/*_MAX
ranges). It IS the mechanism behind finding #5 above (silent target
override with no GUI feedback) - tracked there instead of as a standalone
mystery.
