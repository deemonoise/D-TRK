# Teensy UI parity wave

Reference: freshly fetched origin/feature/teensy 3e72836b1a2d03bd8a32d48d84d43fcb4545220f.
Reference worktree: /private/tmp/dtrk-ui-teensy-reference (detached, read-only).
Starting dirty ESP snapshot: /private/tmp/dtrk-ui-wave-baseline. Preserve both completed waves.

User objective: port all more logical screen/navigation changes from Teensy, then immediately OTA
192.168.3.221 when verified. Personal production build: WT32 N16R2 qio_qspi, first track button
P12/bit10; restore repository pins after build. No git commit/push authorized.

Scope: UI parity for available ESP functions, retain old eight-machine FM, KIT8, 32kHz,
transactional saves, local sample/WT tools, matrix guard, PERF without triplets and known Wi-Fi RAM
handling. A scope clarification is pending for new BLE MIDI/REC/expanded export backends; independent
UI work proceeds without adding dead controls. Do not silently import UART/RPC or unsupported DSP.

Owners:
- storage_port: FILE/BANK, import/render/bounce dialogs, narrow real UI host harness.
- editor_song_port: INST/PROJ/sample/WT/presets, pure proj_pages and sliceNote helpers/tests.
- audio_bench_port: GRID/SONG/TRACK/PERF and associated operation forms.
- root: shared App/navigation/raw touch, ActionDialog/Menu/ParamList/common, integration,
  review, native/host/WT32 verification, docs and authorized OTA + Wi-Fi round-trip smoke check.

Acceptance:
- All remaining reference screen changes accounted for as ported/already-parity/hardware-backend only.
- FILE organization, child menus, footers, per-page selection, grid cycle and contextual labels match
  reference where ESP supports actions; no silent regression of completed features.
- Pure behavior and failure/gesture/cancel tests before relevant changes, existing tests remain green.
- No unsupported or dead entry points; ordinary local APIs remain usable without Teensy link.
- Native, host ASan/UBSan, WT32 build, clangd and applicable Unclebob checks honestly reported.
- Concrete diff reviewed, no commits. OTA ordinary app image, NVS unchanged, personal P12 mapping.
- After reboot user reopens Wi-Fi, verify HTTP, new preset read/write/delete and unchanged crashlog.
