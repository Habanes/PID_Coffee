# CoffeePID - Update Plan (closed out 2026-09-20)

All items from this plan have shipped. Originally superseded
`planned updates.md` after a full grill session (rotary invert, decimal
drop, shot-time default, Eco/Sleep two-tier, the 8-view menu + brewing
phase display, and the preset system rework). Each is documented in
`DOCUMENTATION.txt` - see sections 1, 4.1, 4.2, 6.2, 6.3, 7, 8.1, 8.2, 8.3.

Kept as a pointer rather than deleted outright in case any of the
"careful about" notes below are useful context for follow-up work:

- **Preset rework is a breaking NVS change, no migration.** First boot after
  this update resets to a single "default" preset (slot 0); any previously
  tuned presets need to be redone once. Same precedent as the earlier
  `tempOffset` removal - orphaned old NVS keys are harmless, just unused
  flash bytes.
- **Preset storage is a single NVS blob** (`writeNvsLocked()` writes the whole
  `preset[MAX_PRESETS]` array under one "presets" key), not one key per field
  per slot - an earlier per-key version (~200 entries for 20 slots) actually
  shipped briefly and had a real bug: adding a new preset allocates fresh NVS
  entries, which silently failed once the partition filled up, so newly-added
  presets vanished on reboot while everything else kept working. Fixed before
  this note was written by switching to the single-blob scheme - see the
  PRESET STORAGE note in Settings.h. Per-tick encoder edits still don't touch
  NVS at all under the working-copy model, only explicit save/update/select/
  delete/rename/global-apply do.
- **Bench-test the preset save flow specifically** (7-seg long-press ->
  "OrXX" screen -> rotary -> short-press, and its web GUI equivalent) before
  relying on it - it's the newest, least-exercised piece of UI in this pass.
  In particular: add a preset, reboot, confirm it's still there (the bug
  above).
- PID retuning (more aggressive, less accurate, per your original note) is
  still on you to do manually - nothing in this plan touched the PID gain
  defaults.
