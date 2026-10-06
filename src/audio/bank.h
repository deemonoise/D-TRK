#pragma once
#include <stdint.h>
#include "sample_bank.h"
#include "synth.h"

namespace mt {
struct Project;
}

namespace audio {

// Sample bank in the "samples" partition, mounted by audio::begin(). Reads (list, data) from any
// task; changes only through the functions below (UI task), which pause the audio task while
// they write flash.
mt::SampleBank& bank();
bool bankMounted();
// Lookup by name for the synth (audio task): the name is resolved through the project's sample
// list to the bank entry of its data key. The list and the bank change only with the engine stopped.
const mt::SampleSource* sampleSource();
// Wavetable lookup for the synth (audio task, note-on): a built-in "*NAME" is its bank entry, any
// other name goes through the project's wavetable list to the entry of its key (mt::wtKey).
const mt::WtSource* wavetableSource();
void bankSetProject(const mt::Project* p);

enum class BankResult : uint8_t {
  Ok, Busy, NoSd, NoBank, OpenFail, ReadFail, NotWav, Unsupported, Truncated, Full, WriteFail, NoMemory
};
const char* bankResultText(BankResult r);  // short, upper case, for toasts

// done / total in work units (bytes of the source file for import, bytes moved for compact).
using BankProgressFn = void (*)(uint32_t done, uint32_t total, void* ctx);

// Imports a WAV from the SD card into the bank cache under its data key (mt::sampleKey), making
// room by evicting entries p does not use: mono, <= 32 kHz, root from the "smpl" chunk or 60. When
// the file's "mtcr" crc (or knownCrc) is cached with the same length, the data is not read at all.
// Busy while the engine plays. The caller adds the result to the project's list.
struct ImportOut {
  uint32_t crc, frames;  // bank data
  uint8_t root;
};
BankResult importToCache(const char* path, const mt::Project& p, ImportOut& out, const uint32_t* knownCrc = nullptr,
                         BankProgressFn cb = nullptr, void* ctx = nullptr);
// Writes bank entry i as a mono 16-bit WAV with root and "mtcr" crc to path (via path.tmp).
// Works while the engine plays (reads mapped flash only).
BankResult exportWav(int i, const char* path);
// Imports a wavetable WAV (mt::wtImport layouts, "clm " frame size) from the SD card into the bank
// cache under its key (mt::wtKey of the canonical source crc, returned in crc), making room by
// evicting entries p does not use. When the file is a canonical mono 16-bit source whose "mtcr" crc
// (or knownCrc) is cached already, nothing is read. Busy while the engine plays. The caller adds the
// result to the project's wavetable list.
BankResult importWtToCache(const char* path, const mt::Project& p, uint32_t& crc, const uint32_t* knownCrc = nullptr,
                           BankProgressFn cb = nullptr, void* ctx = nullptr);
// Writes wavetable name (built-in "*NAME" or a name in p's list) as its canonical source: mono
// 16-bit WAV, mt::kWtSrcSamples frames, "mtcr" crc = the key crc, via path.tmp. Works while the
// engine plays (reads mapped flash only).
BankResult exportWt(const char* name, const mt::Project& p, const char* path);
// Reads the first maxMs of a WAV from the SD card for preview: mono, <= 32 kHz, into a new PSRAM
// buffer (*data, heap_caps_free it). Works while the engine plays.
BankResult loadWavPreview(const char* path, uint32_t maxMs, int16_t** data, uint32_t* frames, uint32_t* rate);
BankResult compactBank(BankProgressFn cb = nullptr, void* ctx = nullptr);
// Removes every bank entry p does not use; *removed = count.
BankResult clearCache(const mt::Project& p, int* removed = nullptr);
// Old project without a sample list (mt::migrateSamples), with /projects/legacy.idx on the SD
// card as the legacy index. *missing = sample names left without data. Busy while the engine plays.
BankResult migrateProject(mt::Project& p, int* missing);
// Old sample with this data in /projects/legacy.idx: the project that migrated it first (its folder
// holds the file after that project's sync) and the old name. False if unknown.
bool legacySource(uint32_t crc, uint32_t frames, char project[17], char name[17]);

// audio.cpp -> bank.cpp
void bankBegin();

}  // namespace audio
