# Trance gate in the track insert — plan

Design: `2026-10-14-trance-gate-design.md` (all recommended options, Q7 = b: bypassed while the
transport is stopped). TDD per task.

1. **Model / fx**: TrackCfg (end) `gateMask = 0x5555, gateRate = 1, gateDepth = 0, gateLen = 100,
   gateAtk, gateRel`; `GateRate` (1/32 1/16 1/8), `gateMs()`, `kGatePresets[]` + `gatePresetOf()`;
   `Fx::GTE` before Count (kOrder after SRR, synth only, OFF / 1..127), `kLockGte`. storage.cpp
   static_asserts. Tests: test_model, test_fx_info.
2. **Synth**: song clock `gatePos_` (1/16 steps, wraps at 64): `0xF6 5 idx` re-anchors at its sample,
   free-runs per segment while running and not paused; `0xFA` = 0 and running; new `0xFD` (stop) =
   not running (gate bypassed, gain ramps back to 1). Strip gate after the crush, before sends /
   meter / pan; depth from the newest voice (GTE lock or TrackCfg); snaps on a strip snap.
   `gatePos()` for the UI playhead. Tests: test_synth.
3. **Sequencer**: absolute 1/16 counter since start (`gateTick_`, rewound with the step history),
   `0xF6 5 idx` next to the PERF clock; `stop()` sends `0xFD` to the synth. Tests: test_sequencer.
4. **File**: TINS record 11 bytes (… driveModel, mask lo, hi, rate, depth, len, atk, rel);
   `readTinsRecord` assigns only present fields (old 3 / 4-byte records keep mask 0x5555, depth 0).
   Tests: test_project_io.
5. **UI**: TRACK INS rows Trance, Pattern, Steps (cell editor: turn = cursor, tap / track buttons
   toggle, A + turn = toggle and advance, Shift + turn = rotate; cancel restores the mask), Rate,
   Length, Attack, Release; dimmed while Trance = OFF; playhead from `audio::gatePos()`.
6. **Demos / manual**: one pad track gated; manual: rows, fx GTE, gate section; DTY + GTE in the fx
   order list, the INT-only fx paragraph and #trackfx.
7. Verify: `pio test -e native`, `pio run -e wt32`, `pio run -e wt32-pool24`.
