#pragma once
#include <stdint.h>
#include "bank_ops.h"
#include "onset.h"

// The sample bank lives on the synth board (src/teensy/bank.cpp): these are its requests over the
// link. UI task only, blocking (the long ones report progress). Whatever depends on the project's
// sample / wavetable lists first waits (pumping the mirror, up to 2 s) until the synth board has
// them. Bank changes are refused while the engine plays or is paused (Busy).
namespace audio {

using BankResult = mt::BankResult;
using ImportOut = mt::ImportOut;
const char* bankResultText(BankResult r);  // short, upper case, for toasts

// done / total in work units (an import: frames of the file; a sync / save: items x 1000; compact:
// bytes moved).
using BankProgressFn = void (*)(uint32_t done, uint32_t total, void* ctx);

// A WAV on the card into the cache under its data key: mono, <= 44.1 kHz, root from "smpl" or 60.
// When the file's "mtcr" crc (or knownCrc) is cached with the same length, the data is not read.
// The caller adds the result to the project's list.
BankResult importToCache(const char* path, ImportOut& out, const uint32_t* knownCrc = nullptr,
                         BankProgressFn cb = nullptr, void* ctx = nullptr);
// A wavetable WAV (mt::wtImport layouts) into the cache under mt::wtKey of its canonical source.
BankResult importWtToCache(const char* path, uint32_t& crc, const uint32_t* knownCrc = nullptr,
                           BankProgressFn cb = nullptr, void* ctx = nullptr);

// Sync / save outcome: entries left missing (bits by list index), files the save could not write.
struct BankSets {
  int missing = 0, failed = 0;
  uint8_t samples[16] = {}, wavetables[4] = {};
  bool sampleMissing(int i) const { return i >= 0 && i < 128 && (samples[i >> 3] >> (i & 7)) & 1; }
  bool wtMissing(int i) const { return i >= 0 && i < 32 && (wavetables[i >> 3] >> (i & 7)) & 1; }
};
// Sync / save progress: item = the list entry being worked on (samples 0..127, wavetables
// kItemWt + i, -1 none); done / total = items x 1000.
constexpr int kItemWt = 128;
using BankItemFn = void (*)(int item, uint32_t done, uint32_t total, void* ctx);
// Every entry of the lists not cached comes from /projects/<name>/ (wavetables: .../wt/); name ""
// = only the cache is checked.
BankResult syncProject(const char* name, BankSets* out = nullptr, BankItemFn cb = nullptr, void* ctx = nullptr);
// Every cached entry of the lists whose file in /projects/<name>/ is not current is written there.
// WriteFail when any could not be. Works while the engine plays.
BankResult saveAssets(const char* name, BankSets* out = nullptr, BankItemFn cb = nullptr, void* ctx = nullptr);
// Removes every bank entry the project does not use; *removed = count.
BankResult clearCache(int* removed = nullptr);
BankResult compactBank(BankProgressFn cb = nullptr, void* ctx = nullptr);

// The bank as the synth board sees it with the project's lists, fetched again after any bank change,
// a list change or a synth reboot. ok false: no answer / no bank (the rest is zero).
struct BankView {
  bool ok = false;
  bool mounted = false;
  int count = 0;
  uint32_t capacity = 0, free = 0, unused = 0, gen = 0;
  uint8_t samples[16] = {}, wavetables[4] = {};
  uint8_t builtins = 0;
  uint16_t rate[128] = {};
  bool sampleCached(int i) const { return i >= 0 && i < 128 && (samples[i >> 3] >> (i & 7)) & 1; }
  bool wtCached(int i) const { return i >= 0 && i < 32 && (wavetables[i >> 3] >> (i & 7)) & 1; }
};
const BankView& bankView();
void bankViewStale();

// The bank entry of project sample index (from the bank view's figures plus a SampleInfo request).
struct SampleInfo {
  bool cached = false;
  uint32_t frames = 0, rate = 0;
  uint8_t root = 60, loop = 0;  // as imported ("smpl"), the instrument's defaults
};
BankResult sampleInfo(int index, SampleInfo& out);

// Wavetable name (built-in "*NAME" or in the project's list) is in the bank (from the bank view).
bool wtCached(const char* name);

// Level 0 of frame f of wavetable name (built-in or in the project's list), >> 8, 256 points.
// Cached here (a few frames). False when absent / no answer.
bool wtFrame(const char* name, int f, int8_t* out);

// Sample editor, on the synth board's data of project sample index. wavePeaks: min / max (>> 8) of
// cols columns from grid column col0 of a zoom grid putting span frames on width columns
// (mt::wavePeaks; empty columns min > max). onsets: mt::detectOnsets of the whole sample into out
// (up to max), the count, -1 on failure (*err says why).
BankResult wavePeaks(int index, uint32_t col0, uint32_t span, int width, int cols, int8_t* mn, int8_t* mx);
int onsets(int index, mt::Onset* out, int max, BankResult* err = nullptr);

// A WAV on the synth board's card (FILE import list) streamed to its output from the start, ending
// the previous one; frames / rate of the file. previewStop() ends it.
BankResult previewFile(const char* path, uint32_t* frames = nullptr, uint32_t* rate = nullptr);
void previewStop();

}  // namespace audio
