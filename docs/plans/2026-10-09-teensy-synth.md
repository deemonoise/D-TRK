# Teensy 4.1 Synth Board Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** All sound and all files move from the ESP32-S3 to a Teensy 4.1 (PCM5102A on I2S1, its own SD, sample bank in its flash); the engine goes stereo, 44.1 kHz, 32 voices. ESP keeps sequencer, UI, MIDI OUT, Wi-Fi firmware page.

**Architecture:** Full-duplex UART (ESP GPIO 13/14 ↔ Teensy Serial1, 3 Mbaud), COBS frames with CRC16. ESP streams time-stamped synth events (RT) and mirrors the sound state by diffing a `SynthModel` (a base struct of `Project`) against a shadow copy; files go through a remote `fs::FS` served by SdFat on the Teensy. Teensy runs `mt::Synth` from `lib/core` inside a Teensy Audio Library `AudioStream`, owns the sample bank (flash) and does all sample-data work (import, peaks, onsets, previews, render).

**Tech Stack:** C++17, PlatformIO: ESP32-S3 Arduino (pioarduino), Teensy 4.1 Arduino (Teensyduino: Audio, SdFat), Unity native tests, FlasherX (vendored).

Design: `docs/plans/2026-10-09-teensy-synth-design.md`. **No commits** (user rule): skip every commit step; the user commits on request.

Conventions for every task:
- Native tests: `pio test -e native -f <test_dir>`; whole suite `pio test -e native` (baseline 870 passing).
- Builds: `pio run -e wt32`, `pio run -e teensy41` (from Task 1 on). Both must build at the end of every task.
- Match the surrounding code: 2-space indent, `mt::` in `lib/core`, comments only where the existing code would have them, English in code.
- Stages end with a **hardware check** list; the user runs it. Do not stop between tasks (autonomous-finish rule) unless a build or test cannot be made to pass.

---

## Stage 1 — Teensy target, sine out, UART echo

### Task 1: `teensy41` env and source split

**Files:**
- Modify: `platformio.ini`
- Create: `src/teensy/main.cpp`

**Step 1:** In `platformio.ini` add to `[env:wt32]`: `build_src_filter = +<*> -<teensy/>` (inherited by `wt32-ota`, `wt32-*bench`, `wt32-pool*`). Add:

```ini
; Synth board: Teensy 4.1 + PCM5102A on I2S1. First flash over USB (Teensy Loader), later from
; the ESP (PROJ -> SYS -> Update synth, or the /firmware page).
[env:teensy41]
platform = teensy
board = teensy41
framework = arduino
build_flags = -std=gnu++17 -DUSB_SERIAL
build_unflags = -std=gnu++14
build_src_filter = +<teensy/>
lib_ignore = flasherx_esp
test_ignore = *

; Voice pool bench on the Teensy: results over USB Serial and the link Log.
[env:teensy41-bench]
extends = env:teensy41
build_flags = ${env:teensy41.build_flags} -DAUDIO_BENCH_POOL
```

**Step 2:** `src/teensy/main.cpp`: `AudioSynthWaveformSine` 440 Hz → `AudioOutputI2S` (both channels), `AudioMemory(8)`; `loop()` echoes every byte read from `Serial1` (begun at `kLinkBaud`, temporary local constant 3000000) back to `Serial1`, prints counts to `Serial` once a second.

**Step 3:** `pio run -e teensy41` and `pio run -e wt32` build. `lib/core` must compile for the Teensy (`hot.h`: add `#elif defined(__IMXRT1062__)` → `#define MT_HOT FASTRUN`, include `<Arduino.h>` there). Fix any other ESP-only include `lib/core` pulls in (grep `esp_` in `lib/core/src`).

### Task 1b: desk mode — Teensy alone on USB (no DAC, no ESP)

The user can only plug the bare Teensy into the computer for now. Make every Teensy stage testable that way:

**Files:** `platformio.ini`, `src/teensy/main.cpp`, `src/teensy/link_server.cpp` (from Task 8 on), create `scripts/link_desk.py`

- Env `[env:teensy41-desk]`: `extends = env:teensy41`, `build_flags = ${env:teensy41.build_flags} -DLINK_DESK`, `build_unflags = -DUSB_SERIAL`, `-DUSB_MIDI_AUDIO_SERIAL`.
- `LINK_DESK`: audio goes to `AudioOutputUSB` (the computer sees a USB sound card) in addition to I2S; the link runs over USB `Serial` instead of `Serial1` (the `Log` frames then share that port — the desk script decodes them; plain prints off).
- `scripts/link_desk.py` (pyserial, Python 3, run with `python3 -I`): implements the ESP side of the protocol from `link_frame` / `link_msg` (COBS, crc16, `Hello`, `Time` every 50 ms, `EvBatch`, fs ops) with commands: `hello`, `notes` (plays a C-major arpeggio on track 0), `ls <dir>`, `get/put <path>`, `status` (prints `Status`), `bench`. Keep it small; it grows with each stage that adds messages.
- Instruments without an ESP: under `LINK_DESK` the Teensy fills its own `model` at boot from a demo project (`mt::demos`, the `SynthModel` part of a `Project` built on the heap and copied), so `notes` is audible without `StateSet`. The script does not build `SynthModel` bytes itself (layout in Python would be fragile).

**Desk check (instead of the stage hardware checks while the ESP is not wired):** USB audio device appears; `link_desk.py hello` / `notes` / `status` work and are heard through the computer; later stages: `ls`, `get/put` against the Teensy card, bank import of a WAV and a note on a SAMPLE instrument, `bench` results.

### Task 2: ESP side UART echo test (temporary)

**Files:** Create `src/link/link_uart.h`, `src/link/link_uart.cpp`; modify `src/hw/pins.h`, `src/main.cpp`

- `pins.h`: remove `kI2sBclk/kI2sWs/kI2sDout`; add `kLinkTx = 13`, `kLinkRx = 14`, `kLinkSpare = 21` with a comment (Teensy Serial1 RX1 0 / TX1 1 / pin 2).
- `link_uart`: `bool uartBegin(uint32_t baud)` on `UART_NUM_1` via ESP-IDF `uart_driver_install` (RX buffer 8 KB, TX 4 KB), `int uartRead(uint8_t*, int, TickType_t)`, `int uartWrite(const uint8_t*, int)`.
- Under `-DLINK_ECHO_TEST` only: `main.cpp` sends 64 KB of an LFSR pattern in 256-byte bursts, reads the echo, logs bytes/s and mismatches at 3, 4, 5 Mbaud. Add env `[env:wt32-echo]` (`extends = env:wt32`, flag added).

**Hardware check (Stage 1):** wire per design §1; Teensy on USB: 440 Hz in the phones; `wt32-echo` log shows 0 mismatches at 3 Mbaud (note the highest clean rate for Task 3's `kLinkBaud`).

---

## Stage 2 — Link protocol, notes over the link

### Task 3: COBS + frame codec (`lib/core`)

**Files:** Create `lib/core/src/link_frame.h`, `lib/core/src/link_frame.cpp`, `test/test_link_frame/test_main.cpp`

API:

```cpp
namespace mt::link {
constexpr uint32_t kBaud = 3000000;      // both firmwares; raise after the echo test
constexpr int kMaxPayload = 512;
constexpr int kMaxFrame = kMaxPayload + 4;            // type, seq, crc16
constexpr int kMaxEncoded = kMaxFrame + kMaxFrame / 254 + 2;  // COBS overhead + 0x00
uint16_t crc16(const uint8_t* d, int n);              // CCITT-FALSE, init 0xFFFF
// type, seq, payload -> COBS bytes ending in 0x00. Returns the encoded length.
int encode(uint8_t type, uint8_t seq, const uint8_t* payload, int n, uint8_t* out);
// Byte-at-a-time decoder: feed() returns true when a whole valid frame is ready (type(), seq(),
// payload(), size()); bad CRC / overlong / bad COBS drop the frame and bump errors().
class Decoder {
 public:
  bool feed(uint8_t b);
  uint8_t type() const; uint8_t seq() const;
  const uint8_t* payload() const; int size() const;
  uint32_t errors() const;
 private: /* raw buffer kMaxEncoded, decoded buffer kMaxFrame, counters */
};
}
```

**Step 1: failing tests** — round trip empty / 1 / 254 / 255 / 512-byte payloads with zeros and 0xFF runs; corrupt one byte → no frame + `errors()==1`, next frame decodes (resync on 0x00); garbage before the first 0x00 is ignored; payload > `kMaxPayload` rejected by `encode` (returns 0); crc16 of "123456789" == 0x29B1.
**Step 2:** run, see FAIL (missing header). **Step 3:** implement. **Step 4:** PASS.

### Task 4: message codec (`lib/core`)

**Files:** Create `lib/core/src/link_msg.h`, `lib/core/src/link_msg.cpp`, `test/test_link_msg/test_main.cpp`

- `enum class Msg : uint8_t` with every type from design §2 table (`Hello=1, Time, Ev, StateSet, Ack, Nack, FsOpen, FsRead, FsWrite, FsClose, FsStat, FsList, FsRemove, FsRename, FsMkdir, BankSync, AssetsSave, BankImport, SampleInfo, BankIndex, BankClear, WavePeaks, Onsets, PreviewNote, PreviewSlice, PreviewFile, PreviewStop, RenderStart, RenderBlocks, RenderEnd, Meters, Phones, Profile, Status, Progress, FwFromFile, Log`). Keep the numbers stable (append only).
- `constexpr uint16_t kProtocol = 1;`
- Little-endian `Writer` / `Reader` helpers (`u8, u16, u32, u64, bytes, str` with length byte; `Reader::ok()` false after an overrun).
- Structs + `encode(const X&, Writer&)` / `bool decode(Reader&, X&)` for: `Hello {proto, fw[16], bootId u32}`, `Time {tUs u64}`, `EvBatch {n; {tUs u64, track, len, b[3]}[≤ 40]}`, `StateSet {chunk u16, offset u16, data ≤ 480}` (a chunk larger than one frame is split by offset), `Status {cpuPct u8, stalls u16, late u16, voices u8, outPeak u16, trackPeak u8[16], lost u16, scopeN u8 (0 or ≥1), scope int8[≤ 256 after decimation]}`, `Progress {op u8, done u32, total u32}`, `FsResult {err i16, ...}` per fs op (see Task 11), `Log {text}`.
- **Tests:** round trip every struct, truncated input → `decode` false, `EvBatch` of 40 fits `kMaxPayload`.

### Task 5: time sync (`lib/core`)

**Files:** Create `lib/core/src/time_sync.h`, `lib/core/src/time_sync.cpp`, `test/test_time_sync/test_main.cpp`

```cpp
namespace mt::link {
// Teensy side: maps ESP engine time to local micros(). offset = min over a 2 s window of
// (local arrival - esp stamp): the minimum drops queueing delay, the window follows drift.
class TimeSync {
 public:
  static constexpr uint32_t kWindowUs = 2000000;
  void sample(uint64_t espUs, uint64_t localUs);
  bool valid() const;
  uint64_t toLocal(uint64_t espUs) const;  // espUs + offset
 private: /* ring of 64 {localUs, delta}, recompute min on insert / expiry */
};
}
```

Tests: constant delay 300 µs + random 0..2 ms jitter → `toLocal` error < 50 µs; drift 50 ppm over 60 s → error stays < 100 µs; one late sample (20 ms) does not move the offset; invalid before the first sample.

### Task 6: `SynthModel` as base of `Project`

**Files:** Create `lib/core/src/synth_model.h`, `lib/core/src/synth_model.cpp`, `test/test_synth_model/test_main.cpp`; modify `lib/core/src/model.h`, `lib/core/src/synth.h`, `lib/core/src/synth.cpp`, `lib/core/src/model.cpp` (only if `reset()` needs it)

- `synth_model.h`: move from `Project` into `struct SynthModel` the fields `Synth` reads (grep `p_\.` in `lib/core/src/synth*.cpp`): `bpm, scaleRoot, scaleType, tracks, instruments, masterVol, dlyTime, dlyFb, dlyTone, dlyLevel, rvbSize, rvbDamp, rvbLevel, compAmt, compRel, scTrack, scDepth, djFilter, samples, sampleCount, wavetables, wavetableCount` — same types, defaults, comments. `TrackCfg`, `Instrument`, `ProjSample`, `ProjWavetable` stay in `model.h`; `synth_model.h` is included by `model.h` after them (or the types move to a small header if include order needs it).
- `struct Project : SynthModel { ... }` keeps everything else; `Project::reset()` still resets every field. No call site changes.
- Chunk table for the mirror:

```cpp
enum class Chunk : uint16_t { Tracks = 0, Master = 1, Samples = 2, Wavetables = 3, Instr0 = 16 };  // Instr0 + i
// Byte range of a chunk inside SynthModel; false for an unknown id.
bool chunkRange(uint16_t id, uint32_t& off, uint32_t& len);
constexpr int kChunks = 4 + kInstruments;
uint16_t chunkId(int i);  // i-th chunk in send order
```

  Master = `masterVol..djFilter` plus `bpm, scaleRoot, scaleType` — make them contiguous in the struct and `static_assert` the ranges with `offsetof`.
- `Synth(const SynthModel&)`; `p_` becomes `const SynthModel&`. `OfflineRender` keeps `Project&`.
- **Tests:** chunk ranges cover every byte of `SynthModel` exactly once (sum of lens + padding check via a memcmp after copying chunk by chunk into a zeroed model); changing `instruments[5].vol` changes only chunk `Instr0+5`.
- Run the whole native suite: all 870 still pass.

### Task 7: mirror diff (`lib/core`)

**Files:** Create `lib/core/src/state_mirror.h`, `lib/core/src/state_mirror.cpp`, `test/test_state_mirror/test_main.cpp`

```cpp
namespace mt::link {
// ESP side: which chunks differ from what the Teensy acknowledged.
class StateMirror {
 public:
  void invalidate();                      // Teensy rebooted / new project: everything is dirty
  // Next dirty chunk of `live` not in flight, -1 if none. Marks it in flight.
  int nextDirty(const SynthModel& live);
  void acked(uint16_t chunk, const SynthModel& sent);  // copies the sent bytes into the shadow
  void failed(uint16_t chunk);            // not in flight any more, stays dirty
  bool synced(const SynthModel& live) const;  // nothing dirty, nothing in flight
 private: SynthModel shadow_; bool valid_[kChunks]; bool inFlight_[kChunks];
};
}
```

Tests: fresh mirror → all chunks dirty in `chunkId` order; after acking all, `synced`; edit two instruments → exactly those two; `invalidate` → all again; an edit made while the chunk is in flight stays dirty after the ack of the older bytes (ack copies the *sent* snapshot, not live).

### Task 8: Teensy link server + audio output

**Files:** Create `src/teensy/link_server.h/.cpp`, `src/teensy/audio_out.h/.cpp`, `src/teensy/ev_queue.h`; rewrite `src/teensy/main.cpp`

- `audio_out`: `class SynthStream : public AudioStream` (0 inputs, 2 outputs). `update()`: stamp `blockStartUs = micros()` (+ the known output latency), drain `EvQueue` for events whose local time falls before the block end → `synth.event(offset, ...)`, late ones at 0 (`late++`); `synth.render(l, r)` (until Task 22: mono `render` copied to both); `transmit` both blocks; measure cycles (`ARM_DWT_CYCCNT`) → CPU %, `setLoad`. Objects: `SynthStream` → `AudioOutputI2S` (L, R). `AudioMemory(12)`.
- `ev_queue.h`: SPSC ring (256 entries of `{uint64_t localUs; uint8_t track, len, b[3]}`), producer `loop()`, consumer the audio ISR; `std::atomic` indices.
- `SynthModel model;` global in DTCM; `mt::Synth synth(model);` (no bank / wavetables yet).
- `link_server`: `Serial1.begin(kBaud)`, `addMemoryForRead/Write` 8 KB each; `poll()` feeds the decoder; handles `Hello` (reply own Hello: `kProtocol`, version from `-DFW_VERSION` (reuse `scripts/version.py` via `extra_scripts`), random `bootId` from `ARM_DWT_CYCCNT` at boot), `Time` → `TimeSync`, `EvBatch` → `toLocal(t) + kPlayLatencyUs (4000)` → `EvQueue`, `StateSet` → `__disable_irq` around the chunk `memcpy` into `model` → `Ack{chunk}` (chunk > one frame: apply on the last piece only, pieces buffered in a 6 KB scratch), `Status` every 40 ms.
- `Log`: `linkLog(fmt, ...)` printf into a `Log` frame (max 200 chars), also to USB Serial.

### Task 9: ESP link client + `audio::` over the link

**Files:** Create `src/link/link.h`, `src/link/link.cpp`; delete `src/audio/audio.cpp`, `src/audio/wt_bench.cpp`; rewrite `src/audio/audio.h` (keep namespace `audio`, keep only the live API); modify `src/main.cpp`, `src/engine/engine.cpp` (no change expected: it calls `audio::post`)

- `link` task (core 0, prio 5): owns `link_uart`; RX: decoder → dispatch (`Hello`, `Ack/Nack`, `Status`, `Progress`, `Log` → `Serial`, fs/bank replies); TX: RT ring (events, Time) first, then the request queue. `Time` every 50 ms. Events posted from the engine accumulate for at most 1 ms or 40 events into one `EvBatch`.
- Request API for the UI task: `bool request(Msg type, const uint8_t* p, int n, Msg replyType, uint8_t* reply, int& replyLen, uint32_t timeoutMs)` — one outstanding request (mutex), 3 retries, timeout 200 ms default, 2000 ms for fs / bank ops.
- State: `bool synthUp()` (Hello seen, Status within 1 s), `bool versionOk()`, `const char* synthFw()`, counters (`lost`, `crcErrors`, `late`, `stalls`, `retries`).
- Mirror pump (UI task, every frame, `App::loop`): `StateMirror::nextDirty(project)` → `StateSet` pieces → `acked/failed`. On a new `bootId` or after `projectReplaced`: `invalidate()`. `bool audio::synced()` = mirror synced (+ bank sync, Task 17).
- `audio.h` keeps: `begin(Project*)` (starts the link), `post`, `preview`, `previewSlice`, `takeLoad`, `scopeRead`, `scopePeak`, `trackPeaks`, `setMeters`, `setPhones`, `profileStart/Running/Stop`, `pollLog`, `kRate = 44100`, `kBlock = 128`. Values come from the last `Status`. Remove `reserve`, `synthInternal`, `reverb*`, `previewBuffer/previewStop/previewPlaying` (Task 20 adds file preview), `Paused`, `liveSynth`, `pauseForFlash`.
- Fix every call site that used removed functions so `wt32` builds; where the feature lands in a later task, leave the UI action disabled with a `// Task N` note and a toast "not yet".

**Hardware check (Stage 2):** wire ESP ↔ Teensy; an INT track plays CHIP / FM / DRUM / SYNTH instruments (no samples) in time with the MIDI OUT of another track; editing an instrument is heard within ~50 ms; status bar CPU shows the Teensy load; unplug / replug Teensy power → `NO SYNTH`, then sound back without touching the ESP.

---

## Stage 3 — Synth-board states in the UI

### Task 10: NO SYNTH / version / barrier

**Files:** `src/ui/app.cpp`, `src/ui/app.h`, the status bar drawing (grep `cpu` in `src/ui/app.cpp`), `src/ui/proj_screen.cpp`

- Status bar: `NO SYNTH` (red) when `!audio::synthUp()`, `SYNC` (yellow) while `!audio::synced()`.
- Boot: splash text "Synth: connecting…" up to 3 s, then continue.
- `!versionOk()`: full-screen message "Synth firmware <x> does not match (protocol N). Update it: PROJ -> SYS -> Update synth, or Wi-Fi /firmware." with a button opening the Wi-Fi dialog.
- Play pressed while `!synced()`: start is deferred until synced (toast "syncing…"), max 10 s, then start anyway.
- PROJ → SYS rows: `Synth fw`, `Link` (`lost/crc/late` counters), `Card` (Task 18).

---

## Stage 4 — Remote file system, one card

### Task 11: Teensy fs server

**Files:** Create `src/teensy/fs_server.h/.cpp`

- `SD.begin(BUILTIN_SDCARD)` (SdFat via Teensy `SD`); creates `/projects /midi /samples /wavetables /presets/<TYPE> /firmware` like `hw::sdBegin` did.
- Handles: `FsOpen {path, mode r/w/a}` → handle (max 4 open), `FsRead {h, n ≤ 480}` → bytes, `FsWrite {h, bytes}`, `FsClose`, `FsStat {path}` → exists, isDir, size, `FsList {dir, startIndex, max}` → entries `{isDir, size u32, name}` packed until the frame is full + `more` flag, `FsRemove`, `FsRename`, `FsMkdir`, `FsRmdir` (recursive, for project folders). Every reply carries `err` (0 ok, negative = errno-like codes in `link_msg.h`).
- Card removed: `SD.mediaPresent()` check per op, error `kErrNoCard`; re-`begin` on the next op.

### Task 12: ESP `RemoteFS` (`fs::FS`)

**Files:** Create `src/storage/remote_fs.h`, `src/storage/remote_fs.cpp`; rewrite `src/hw/sdcard.cpp` (keep `sdcard.h` API)

- `class RemoteFileImpl : public fs::FileImpl` (read with 4 KB look-ahead in 480-byte requests, write buffered 4 KB, `seek` via cache invalidation + server `FsSeek` — add `FsSeek` to `Msg`, `size`, `position`, `name`, `isDirectory`, `openNextFile` / `getNextFileName` via `FsList` pages of 32, `rewindDirectory`).
- `class RemoteFSImpl : public fs::FSImpl` (`open`, `exists`, `rename`, `remove`, `mkdir`, `rmdir`).
- `hw::sdBegin()` → `RemoteFS` mounted when `audio::synthUp()`; `sdReady()` = synth up and last op not `kErrNoCard`; `sdRecover()` = re-stat `/`. `sdFs()` returns the remote FS. `FileSink/FileSource`, `sdNextEntry`, `sdList*` unchanged (they work on `fs::File`).
- Delete the SPI / `SD.h` code and `kSd*` pins from `pins.h`.
- **Test without hardware:** none native (Arduino `fs::` types). Verified in Task 14's loopback + Stage 4 hardware check.

### Task 13: drop Wi-Fi file manager

**Files:** `src/net/web.cpp`, `src/net/web_page.h`, `src/ui/wifi_dialog.cpp`, `docs/manual*.md` (Task 31)

- Keep `/`, `/api/update` (ESP `.bin`); delete `/api/list|file|upload|rename|delete|mkdir|rmdir` handlers and their helpers; page reduced to the firmware form (Task 28 adds the `.hex` field).
- Wi-Fi dialog text: only the firmware URL.

### Task 14: loopback test of fs over the link (native)

**Files:** Create `test/test_link_loop/test_main.cpp`, `lib/core/src/link_fs.h/.cpp`

To make fs logic testable, put the transport-independent halves in `lib/core`:
- `link_fs`: `FsServerCore` (handles `Fs*` payloads against an abstract `FsBackend` with `open/read/write/close/stat/list/...`), `FsClientCore` (builds requests, parses replies; the Arduino `RemoteFileImpl` wraps it). Teensy `fs_server` = `FsServerCore` + SdFat backend; ESP `remote_fs` = `FsClientCore` + link request.
- Test: client ↔ server through an in-memory "wire" (encode → decode both ways) with a POSIX backend on a temp dir: write 100 KB file in 480-byte pieces, read back, list a dir of 70 entries across pages, rename, remove, rmdir recursive, error on missing file.

**Hardware check (Stage 4):** copy the old ESP card content to a FAT32 or exFAT card in the Teensy; load / save / Save As / delete projects, MIDI import, presets browse / save; a non-ASCII file name shows and is skipped / loaded correctly; pull the card → errors shown, sound keeps playing.

---

## Stage 5 — Sample bank on the Teensy

### Task 15: Teensy flash backend

**Files:** Create `lib/flasherx/` (vendor `FlashTxx.c/.h`, `FXUtil.c/.h` from joepasquariello/FlasherX with its license text in `LICENSES/`), `src/teensy/flash_bank.h/.cpp`

- Flash map constants (Teensy 4.1, 8 MB at `0x60000000`, 4 KB sectors): `kFwMax = 1 MB`, `kOtaBase = 0x60100000`, `kOtaSize = 1 MB`, `kBankBase = 0x60200000`, `kBankSize = 0x60800000 - kBankBase - 256 KB` (top 256 KB left to EEPROM emulation + restore area; verify against `imxrt1062_t41.ld` / `FLASH_RESERVE` and `static_assert` the firmware end from the linker symbol `_flashimagelen` at boot).
- Implements whatever write interface `mt::SampleBank` needs (read `lib/core/src/sample_bank.h` — same role as `src/audio/bank.cpp`'s partition I/O): erase sector, program, memory-mapped read pointer.
- Writes: `AudioNoInterrupts()` + `SynthStream::park()` (renders silence, does not touch bank data) around each erase / program batch.

### Task 16: port bank logic to the Teensy

**Files:** Create `src/teensy/bank.h/.cpp` from `src/audio/bank.cpp` (then delete `src/audio/bank.*`)

- Move `importToCache`, `importWtToCache`, `cacheWrite`, `exportWav`, `exportWt`, `compactBank`, `clearCache`, `bankSetProject` (now: `SynthModel`), `sampleSource`, `wavetableSource`, built-in wavetable generation at first boot — swap `fs::FS` for SdFat `File`, ESP partition for `flash_bank`. Drop `migrateProject`, `legacySource`, `loadWavPreview`.
- `kWavMaxRate` → 44100 (`lib/core/src/wav.h`), update `test_wav` / `test_sample_bank` expectations.
- `synth.setBank(sampleSource()); synth.setWavetables(wavetableSource());`

### Task 17: bank messages

**Files:** `src/teensy/link_server.cpp`, `src/teensy/bank.cpp`; ESP: create `src/link/bank_client.h/.cpp` (namespace `audio`, replaces the old bank API at the call sites)

Teensy handlers (long ops run in `loop()` as a state machine, `Progress` every 100 ms, final reply when done):
- `BankSync {projectName}` — resolve every `model.samples` / `model.wavetables` entry: bank hit by CRC key → ok; else `/projects/NAME/<name>.wav` or `/projects/NAME/wt/<name>.wav` → import; else missing. Reply: missing count + bit mask.
- `AssetsSave {projectName}` — `exportWav` / `exportWt` of every project entry into `/projects/NAME/` (overwrite).
- `BankImport {path, kind sample|wt}` → `{err, name, crc, frames, rate}`.
- `SampleInfo {index}` → `{frames, rate, crc, loaded}`; `BankIndex` → free bytes, entries.
- `BankClear` → `clearCache(model)` result; `compact` flag → `compactBank`.

ESP `bank_client`: `BankResult importToCache(path, ImportOut&)`, `importWtToCache`, `syncProject(name, missing&)`, `saveAssets(name)`, `clearCache`, `compactBank`, `sampleInfo(i, ...)`, `bankResultText` — blocking requests with a progress callback (UI shows the same dialogs as now).

### Task 18: storage + UI call sites

**Files:** `src/storage/storage.cpp`, `src/storage/render_io.cpp`, `src/ui/file_screen.cpp`, `src/ui/inst_screen.cpp`, `src/ui/proj_screen.cpp`, `src/ui/wt_picker.cpp`, `src/ui/preset_browser.cpp`, `src/ui/track_screen.cpp`, `src/ui/app.cpp`, `src/main.cpp`

Replace every `audio::bank()`, `bankMounted()`, `importToCache`, `exportWav`, `migrateProject`, `legacySource`, `Paused`, `liveSynth`, `wavetableSource` use (≈140 sites, grep `audio::`):
- project load → after `loadProject`: `syncProject(name)`; save → `saveAssets(name)` after the `.mtp` is written; the old sample-file loops in `storage.cpp` go away.
- FILE → SAMPLES / import / WT import → `importToCache` over the link.
- Sample frames / rate for the editor and INST pages → `sampleInfo` (cache per index, invalidated on import / load).
- PROJ → SYS: Clear cache / Compact → bank_client; `Card` row from `FsStat("/")` extended with total / free.
- Delete `legacy.idx` handling.

**Hardware check (Stage 5):** projects with samples load (first time: progress while the bank fills), play, save to `/projects/NAME/`; import WAV and WT; Clear cache; a missing sample shows as missing.

---

## Stage 6 — Sample editor and previews

### Task 19: `WavePeaks` and `Onsets`

**Files:** `src/teensy/link_server.cpp` (+ `src/teensy/sample_tools.cpp`), `src/ui/sample_editor.cpp/.h`, `src/link/bank_client.*`

- Teensy: `WavePeaks {index, from, to, cols ≤ 240}` → `int8 min[cols], max[cols]` (one frame: 480 bytes); `Onsets {index, sens, max}` → frame list (u32 each) using `lib/core/src/onset.h` on the bank data.
- ESP editor: draws from peaks fetched on zoom / scroll change (cache keyed by `index, from, to`); the transient snap / CHOP TRANS use `Onsets`. Remove direct bank-data reads.

### Task 20: previews

**Files:** Create `src/teensy/preview_stream.h/.cpp`; modify `src/teensy/link_server.cpp`, `src/ui/file_screen.cpp`, `src/ui/inst_screen.cpp`, `src/ui/grid_screen.cpp`, `src/ui/preset_browser.cpp`

- `PreviewNote {instr, note, ms}`, `PreviewSlice {instr, slice, root, holdMs}` → existing `Synth` preview track logic (port from the old `audio.cpp` preview code).
- `PreviewFile {path}`: SdFat reader in `loop()` fills a 16 KB ring of int16 (WAV via `wav.h` header parsing, mono-mix stereo files, any rate ≤ 48 kHz, linear resample to 44.1); the audio ISR mixes it at sample-instrument level; underrun → silence, counter. `PreviewStop`. ESP: Play in the import list sends `PreviewFile` (replaces `loadWavPreview` + `previewBuffer`).

**Hardware check (Stage 6):** editor waveform at every zoom, CHOP EQUAL / TRANS, Shift snap; import list preview of short and 30 s WAVs; INST / GRID previews.

---

## Stage 7 — Engine: 44.1 kHz, stereo, pan, voices

### Task 21: 44.1 kHz

**Files:** `lib/core/src/synth_osc.h` (`kSynthRate = 44100`), `lib/core/src/synth_comp.h` (`kRate` → `kSynthRate`), `lib/core/src/model.cpp:175-177` (local `kRate` → `kSynthRate`), every test with 32 kHz numbers (grep `32000`, `4000` µs block, `32` samples per ms)

- `kRenderBlockUs` derives itself (2902 µs). Delay line length constant → seconds × `kSynthRate`.
- Run the whole native suite, fix expectations that encode the rate (not behaviour). Note each changed expectation in the task summary.

### Task 22: stereo render

**Files:** `lib/core/src/synth.h/.cpp`, `lib/core/src/synth_delay.h/.cpp`, `lib/core/src/synth_reverb.h/.cpp`, `lib/core/src/synth_comp.h`, `lib/core/src/render.h/.cpp`, tests `test_synth`, `test_synth_delay`, `test_synth_reverb`, `test_render`

- **Tests first:** a centred track renders identical L and R; `Delay` stereo: an impulse into L only comes back on L only; `Reverb` stereo: L ≠ R for a mono impulse (spread), sum energy within 10 % of the mono version; render of a project: L == R when all pans are 0.
- `Synth::render(int16_t* l, int16_t* r)`; voices still render mono into the segment buffer, the mix splits by track pan gains (Task 23; until then 0.7071 both).
- `Delay`: two lines (L, R) of `kDelayLen` each, same params; `setBuffer(buf, len)` takes one buffer of `2 * len`.
- `Reverb`: Freeverb stereo: right channel comb / allpass lengths + 23 (`kStereoSpread`); `kBufLen` doubles.
- `Comp`: detector on max(|L|, |R|), same gain both. DJ filter: two filter states. Master soft clip per channel.
- `OfflineRender::renderBlock(int16_t l[], int16_t r[])`; `normalizePeak` / `trimTail` take interleaved stereo or are called per channel (keep it simple: interleaved `int16_t* lr, frames`).
- Scope: mono sum. Track peaks unchanged.

### Task 23: track pan

**Files:** `lib/core/src/model.h` (`TrackCfg::pan` int8 −64..63 at the end of the struct), `lib/core/src/project_io.cpp` (TCFG / track chunk: write pan, read by chunk size → 0 for old files), `lib/core/src/synth.cpp` (equal-power gains per track, recomputed per block), `src/ui/track_screen.cpp` (row `Pan` L64..C..R63), MIX tab (grep MIX screen: pan under each fader or as an edit row), tests `test_project_io`, `test_synth`

- Tests: old file without pan loads pan 0; save/load round trip −64 and 63; pan −64 → R silent, 63 → L silent, 0 → both 0.7071.

### Task 24: voices and bench on the Teensy

**Files:** `lib/core/src/synth_voice.h` / `voices.h` (`kVoices` → 32 via `MT_VOICES` default 32 on Teensy), `kFmVoiceMax` / heavy cap → `MT_HEAVY_MAX` default 16; `src/teensy/bench.cpp` (port of `AUDIO_BENCH_POOL` from the old `audio.cpp`, results via `linkLog` + USB Serial)

- Memory: put the delay buffer in `DMAMEM` (OCRAM, 2 × 2 s × 44100 × 2 B = 352,800 B), reverb buffer + `Synth` in DTCM. Print `extmem`/free RAM at boot (`linkLog`). If the link reports < 64 KB free, shorten the delay to 1.5 s and note it.

**Hardware check (Stage 7):** SYNTEST / DRMTEST: CPU in the status bar, no clicks, stereo delay / reverb width audible, pan works, `teensy41-bench` results (record the max clean pool in the roadmap / memory).

---

## Stage 8 — Render WAV through the Teensy

### Task 25: split `OfflineRender` into sequence + synth

**Files:** `lib/core/src/render.h/.cpp`, `test/test_render/test_main.cpp`

- `OfflineSequence(Project&, const RenderSpec&)`: the current class minus the synth: `bool nextBlock(Ev* out, int& n)` returns the block's events with sample offsets (current `drain` logic), false when done; length / tail logic unchanged.
- `OfflineRender` becomes `OfflineSequence` + `Synth&` (keeps its tests passing — the render output must be sample-identical to before the split; add a test comparing both paths on a demo project).

### Task 26: render protocol

**Files:** `src/storage/render_io.cpp`, `src/ui/render_dialog.cpp`, `src/teensy/render_server.h/.cpp`

- ESP: `RenderStart {path, tailBlocks, target file|bank, resampleName}` → Teensy parks live audio, resets the synth, opens the WAV (stereo 16-bit 44.1) or a bank write (mono sum, `cacheWrite`); ESP then sends `RenderBlocks {firstBlock, n, events}` (as many blocks as fit a frame, ≤ 8) and waits for `Ack` (Teensy renders them, writes to SD); `RenderEnd {normalize, trim}` → Teensy finalises (two-pass normalize like now: re-read and scale the file), replies `{peak, clips, frames}`; live audio resumes.
- Cancel from the dialog → `RenderEnd {abort}` (file removed).
- Resample-to-sample (`RS1…`) keeps working through the `bank` target.

**Hardware check (Stage 8):** render pattern / song to WAV, open on a computer (stereo, 44.1, correct length / tail); resample into a SAMPLE instrument and play it.

---

## Stage 9 — OTA of the Teensy

### Task 27: update from file on the Teensy

**Files:** Create `src/teensy/fw_update.h/.cpp`

- `FwFromFile {path}` → FlasherX: open the `.hex`, parse into the OTA buffer (`kOtaBase`, `kOtaSize`), check records' checksums, image ≤ `kOtaSize`, image FSEC / reset vector sanity per FlasherX, `Progress` per 4 KB; on success reply ok, then `flash_move` + reboot. Any failure before the move: error reply, buffer erased, nothing else touched.

### Task 28: ESP side: Wi-Fi `.hex` and "Update synth"

**Files:** `src/net/web.cpp`, `src/net/web_page.h`, `src/ui/proj_screen.cpp`, `src/ui/wifi_dialog.cpp`

- `/api/update-synth` (POST, upload): chunks written through `RemoteFS` to `/firmware/teensy.hex.tmp`, renamed to `/firmware/teensy.hex` at the end, then `FwFromFile`; the page shows progress via polling `/api/update-synth/status` (state, %).
- Page: two forms — "Tracker (.bin)" and "Synth board (.hex)".
- PROJ → SYS → `Update synth`: if `/firmware/teensy.hex` exists → confirm dialog with its size → `FwFromFile` → progress → wait for `Hello` with a new version (60 s) → toast with the version.

**Hardware check (Stage 9):** update via Wi-Fi and from the card; a truncated `.hex` is rejected and the old firmware keeps running.

---

## Stage 10 — Clean-up, hardware, docs

### Task 29: ESP clean-up

**Files:** `partitions.csv`, `platformio.ini`, `src/audio/` (whatever is left), `src/storage/crashlog.cpp`

- `partitions.csv`: drop the `samples` partition, give the space to `app0` / `app1` (equal sizes, keep `nvs`, `otadata`, `coredump` if present). Note in the summary: one USB flash needed.
- Remove envs `wt32-wtbench`, `wt32-fxbench`, `wt32-pool24`, `wt32-pool32` and their code paths.
- `crashlog`: if the card is not up at boot, keep the record in RTC memory / NVS and write it once `sdReady()`.
- Grep for dead code: `kSd`, `SD.h`, `i2s`, `heap_caps_malloc` users that only served audio.

### Task 30: enclosure

**Files:** `enclosure/*.scad` (case8), regenerate STL

- Teensy 4.1 bay (61 × 18 mm board, parameters `teensy_*`) on floor ledges next to the DAC / amp bay; microSD slot cut-out on the wall facing the Teensy's SD end, sized for a finger notch; close the ESP card slot opening (wall solid).
- Re-export `case8_*` STL; note "not printed" in the summary.

### Task 31: docs

**Files:** `docs/wiring.md`, `docs/wiring_ru.md`, `docs/manual.md`, `docs/manual_ru.md`, `README.md`, `README_ru.md`, `docs/plans/future-teensy.md`, `docs/plans/2026-10-06-roadmap.md`

- Wiring: DAC to the Teensy, link wires, VUSB–VIN trace, first USB flash of the Teensy, card now in the Teensy (FAT32 / exFAT).
- Manual: Wi-Fi only updates firmware; PROJ → SYS synth rows and Update synth; Pan; stereo; render is stereo 44.1.
- `future-teensy.md`: status header "implemented in feature/teensy, see the design"; keep the streaming section.

### Final verification

- `pio test -e native` all pass; `pio run -e wt32`, `pio run -e teensy41`, `pio run -e teensy41-bench` build.
- Summary for the user: per-stage hardware check lists above, deviations from the design, open items (streaming, PSRAM, INT latency compensation).
