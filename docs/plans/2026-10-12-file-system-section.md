# FILE → SYSTEM (device settings out of PROJ)

## Design

PROJ → SYS holds device settings and diagnostics, not project data. Move it to a third FILE section.

- **FILE**: page bar `PROJECTS | SAMPLES | SYSTEM`; same switching as today (tap the bar, A + turn, B + Shift + turn). SYSTEM is a `ParamList` with the rows moved from PROJ → SYS and their behaviour: Phones, Preview, Theme, Autosave, Firmware, Last reset, Audio RAM, Battery (live refresh), CPU profile (click start / stop).
- **PROJ**: SYS page removed; SONG / FX / COMP / PERF.
- **Preview** becomes a device setting: NVS key `preview` (default ON), saved with the same 1 s debounce as Phones / Theme / Autosave. GRID reads it from App. The project file format is unchanged: the byte is still written, ignored on load.
- **Docs**: `PROJ → SYS → X` becomes `FILE → SYSTEM → X` everywhere; the SYS table moves to the FILE section of the manual.

## Task A: code

`src/ui/file_screen.*`, `src/ui/proj_screen.*`, `src/ui/app.*`, users of `project().preview` (GRID etc.).

## Task B: docs

`docs/manual*.md`, `README*.md`, `docs/wiring*.md`, `enclosure/README*.md`, `docs/plans/2026-10-12-battery-gauge.md`.
