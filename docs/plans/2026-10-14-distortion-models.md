# Track drive models — plan

Design: `2026-10-14-distortion-models-design.md` (all recommended options). TDD per task.

1. **DSP** (`synth_drive.h/.cpp`): `DriveModel` enum (model.h); `ModelDrive` = per-track stateful
   drive: TANH via the stateless `Drive` (perf FX keeps `Drive`), CLIP / FOLD / RECT with first-order
   ADAA, ASYM naive; DC blocker on RECT / ASYM; `reset()`. Speed up `Drive::processBlock` (IRAM, one
   clamp, slope table) within float tolerance. Tests: test_synth_drive.
2. **Model / fx**: `TrackCfg::driveModel` (end), `Fx::DTY` before `Count` (kOrder after DRV, synth
   only, names TNH CLP FLD RCT ASY), `kLockDty`. Tests: test_fx_info, test_model.
3. **Synth**: `Voice::dmodel` from lock / TrackCfg in `control()`; `refreshStrip` sets the model,
   strip snap resets the drive state; fx DTY locks. Tests: test_synth.
4. **File**: TINS record 4 bytes; `readTins` assigns only the fields present; model >= Count -> 0.
   storage.cpp static_asserts. Tests: test_project_io.
5. **UI**: TRACK INS row "Drive type" after Drive, dimmed when drive = 0 or MIDI.
6. **Demos**: one demo track to FOLD or CLIP. **Manual**: Drive type row, fx DTY, insert text.
7. Verify: `pio test -e native`, `pio run -e wt32`, `pio run -e wt32-pool24`.
